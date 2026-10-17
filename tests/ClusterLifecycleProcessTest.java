/* CC BY-NC-SA 4.0; see LICENSE.md. Host-only process test. */
import de.audi.app.terminalmode.smartphone.androidauto2.nav.ClusterLifecycle;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;

public final class ClusterLifecycleProcessTest implements ClusterLifecycle.Effects {
    private Process process;
    private BufferedReader replies;
    private OutputStreamWriter commands;
    private String status;
    private int restores;
    public void publish(String s) throws Exception {
        commands.write(s); commands.flush();
        String line=replies.readLine();
        if(line==null) throw new Exception("native peer closed");
        status=line+"\n";
    }
    public boolean requestOEMRestore() { ++restores; return true; }
    public void log(String s) { }
    private static void check(boolean b,String s) { if(!b) throw new RuntimeException(s); }
    public static void main(String[] args) throws Exception {
        ClusterLifecycleProcessTest e=new ClusterLifecycleProcessTest();
        e.process=Runtime.getRuntime().exec(new String[]{args[0]});
        try {
            e.replies=new BufferedReader(new InputStreamReader(e.process.getInputStream(),"US-ASCII"));
            e.commands=new OutputStreamWriter(e.process.getOutputStream(),"US-ASCII");
            e.status=e.replies.readLine()+"\n";
            ClusterLifecycle c=new ClusterLifecycle("22222222222222222222222222222222",e);
            long now=100, generation=1;
            for(int session=0;session<100;++session) {
                c.activate("mode=fill zoomX=1.0 zoomY=1.0"); c.video(true);
                for(int tick=0;tick<5;++tick) { c.tick(now,e.status); now+=100; }
                check(c.phase()==ClusterLifecycle.RUNNING,"native prepare/render acknowledgement");
                check(c.generation()==++generation,"fresh session generation");
                c.terminate("disconnect");
                c.video(true); // deliberately late notification
                for(int tick=0;tick<3;++tick) { c.tick(now,e.status); now+=100; }
                check(c.phase()==ClusterLifecycle.IDLE,"native stop/restore barrier");
                check(e.restores==session+1,"one restoration per retired native session");
            }
            e.commands.close(); check(e.process.waitFor()==0,"native protocol parser accepted all Java records");
            System.out.println("100 Java/native process reconnect cycles passed (simulated child/display effects)");
        } finally { e.process.destroy(); }
    }
}
