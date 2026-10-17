/* CC BY-NC-SA 4.0; see LICENSE.md. */
import de.audi.app.terminalmode.smartphone.androidauto2.nav.ClusterLifecycle;
import de.audi.app.terminalmode.smartphone.androidauto2.nav.ClusterLifecycleFiles;
import java.io.File;
import java.io.FileOutputStream;

public final class ClusterLifecycleTest {
    private static final String D="11111111111111111111111111111111", O="22222222222222222222222222222222";
    private static final class Effects implements ClusterLifecycle.Effects {
        String command; int restores; boolean canRestore=true, writeFail;
        public void publish(String s) throws Exception { if(writeFail) throw new Exception("write"); command=s; }
        public boolean requestOEMRestore() { ++restores; return canRestore; }
        public void log(String s) { }
    }
    private static void check(boolean b, String s) { if(!b) throw new RuntimeException(s); }
    private static String status(Effects e,String state) {
        String[] words=e.command.trim().split(" ");
        return "M2LS 1 "+words[2]+" "+words[3]+" "+words[4]+" "+words[5]+" "+state+"\n";
    }
    private static ClusterLifecycle start(Effects e) {
        ClusterLifecycle c=new ClusterLifecycle(O,e);
        c.activate("mode=fill"); c.video(true);
        c.tick(100,"M2LS 1 "+D+" 00000000000000000000000000000000 0 0 STOPPED\n");
        check(e.command.indexOf(" STOP\n")>0,"claim stops first");
        c.tick(200,status(e,"STOPPED")); check(c.phase()==ClusterLifecycle.PREPARING,"start after claim");
        c.tick(300,status(e,"PREPARED")); check(e.command.indexOf(" RENDER\n")>0,"resume after ack");
        c.tick(400,status(e,"RUNNING")); return c;
    }
    public static void main(String[] args) throws Exception {
        Effects e=new Effects(); ClusterLifecycle c=start(e); long old=c.generation();
        String stale=status(e,"RUNNING");
        c.terminate("USB unplug"); c.video(true); c.tick(500,stale);
        check(c.phase()==ClusterLifecycle.STOPPING,"late availability cannot revive stopped session");
        check(e.restores==0,"no restore before child exit ack");
        c.activate("mode=letter"); c.video(true); e.canRestore=false;
        c.tick(600,status(e,"STOPPED")); check(c.phase()==ClusterLifecycle.IDLE,"wait restore barrier");
        check(c.generation()==old,"no new generation while restore unavailable");
        e.canRestore=true; c.tick(1600,status(e,"STOPPED"));
        check(c.generation()==old+1 && c.phase()==ClusterLifecycle.PREPARING,"fresh reconnect");
        c.tick(1700,stale); check(c.phase()==ClusterLifecycle.PREPARING,"old ack ignored");
        c.tick(1800,status(e,"PREPARED")); c.tick(1900,status(e,"RUNNING"));
        c.video(false); c.tick(2000,status(e,"RUNNING")); check(c.phase()==ClusterLifecycle.STOPPING,"video loss retires");
        c.tick(2100,status(e,"STOPPED")); check(c.phase()==ClusterLifecycle.IDLE,"loss restores OEM");
        e=new Effects(); c=start(e); c.tick(500,status(e,"STOPPED")); check(!c.isActive(),"helper crash latches failure");
        c.tick(600,status(e,"STOPPED")); check(e.restores==1,"crash restoration follows release");
        e=new Effects(); c=start(e); stale=status(e,"RUNNING"); c.tick(500,stale); c.tick(2601,stale);
        check(!c.isActive() && c.phase()==ClusterLifecycle.STOPPING,"frozen/replayed status times out");
        e=new Effects(); c=start(e); c.tick(500,"M2LS 1 33333333333333333333333333333333 "+O+" 2 5 RUNNING\n");
        check(!c.isActive() && e.command.indexOf(" STOP\n")>0,"daemon restart cannot resume old generation");
        e=new Effects(); c=start(e); c.tick(10,status(e,"RUNNING")); check(!c.isActive(),"clock rollback fail closed");
        e=new Effects(); c=start(e); e.writeFail=true; c.tick(500,status(e,"RUNNING")); check(!c.isActive(),"write failure retires");
        e=new Effects(); c=start(e); c.tick(500,"M2LS 1 garbage\n"); check(!c.isActive(),"malformed status retires");
        e=new Effects(); c=new ClusterLifecycle(O,e); c.activate("mode=fill"); c.video(true); c.tick(100,null);
        check(e.command==null,"old packaged daemon never receives legacy fallback");
        c.tick(200,"M2LS 1 "+D+" 00000000000000000000000000000000 0 0 STOPPED\n");
        c.tick(3301,null); check(!c.isActive(),"unacknowledged command times out");

        File directory=new File(args[0]); directory.mkdirs(); File file=new File(directory,"status");
        if(file.exists()) check(file.delete(),"remove previous test fixture");
        ClusterLifecycleFiles.publish(file.getPath(),"M2LS 1 "+D+" "+O+" 2 3 STOPPED\n",O);
        check(ClusterLifecycleFiles.read(file.getPath()).endsWith("STOPPED\n"),"actual file roundtrip");
        try {
            ClusterLifecycleFiles.publish(file.getPath(),"M2LS 1 "+D+" "+O+" 3 4 PREPARED\n",O);
            check(ClusterLifecycleFiles.read(file.getPath()).endsWith("PREPARED\n"),"atomic replacement");
        } catch(java.io.IOException expected) {
            check(System.getProperty("os.name").startsWith("Windows"),"POSIX atomic rename unexpectedly unavailable");
            check(ClusterLifecycleFiles.read(file.getPath()).endsWith("STOPPED\n"),"unsupported replacement preserves previous snapshot");
            System.out.println("Windows rename-over-existing is unsupported: verified fail-closed; QNX J9 replacement remains a target check");
        }
        FileOutputStream out=new FileOutputStream(file); out.write(new byte[300]); out.close();
        boolean rejected=false; try { ClusterLifecycleFiles.read(file.getPath()); } catch(java.io.IOException expected) { rejected=true; }
        check(rejected,"oversized file rejected");
        System.out.println("Java lifecycle reconnect, crash, restore barrier, stale data and actual file tests passed");
    }
}
