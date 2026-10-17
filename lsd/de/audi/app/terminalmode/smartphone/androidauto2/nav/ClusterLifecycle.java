/* Local P2873 lifecycle adaptation. CC BY-NC-SA 4.0; see LICENSE.md.
 * Java 1.4. Caller serializes events/ticks on ONE integration monitor.
 * Adapted requirements: retire before stop, exact-owner acknowledgements,
 * restore after release, then fresh generation; no copied GPL implementation.
 */
package de.audi.app.terminalmode.smartphone.androidauto2.nav;

public final class ClusterLifecycle {
    public interface Effects {
        void publish(String record) throws Exception;
        boolean requestOEMRestore();
        void log(String message);
    }
    public static final int IDLE = 0, STOPPING = 1, PREPARING = 2, RUNNING = 3;
    private final Effects effects;
    private final String owner;
    private String daemon, action = "STOP", args = "";
    private long generation = 1, sequence, actionSequence, replySequence, lastReply, lastTick, actionTime, restoreTime;
    private int phase = STOPPING;
    private boolean active, available, failed, restorePending, restoreNeeded, acknowledged;

    public ClusterLifecycle(String identity, Effects e) {
        if (!hex(identity)) throw new IllegalArgumentException("owner identity");
        owner = identity; effects = e;
    }
    public void activate(String prepareArgs) {
        if (active) return; // duplicate activation is not a new connection
        active = true; available = false; failed = false;
        args = prepareArgs;
        effects.log("activate; new session waits for release/restoration barrier");
    }
    public void terminate(String reason) {
        active = false; available = false; failed = false;
        stop(reason);
    }
    public void video(boolean value) {
        if (!active || failed) { effects.log("ignore video outside active session"); return; }
        if (value == available) return;
        available = value;
        if (!value) stop("video unavailable");
    }
    private void stop(String reason) {
        if (phase != STOPPING && (phase != IDLE || restoreNeeded)) {
            phase = STOPPING; action = "STOP"; actionSequence = 0; acknowledged = false;
            effects.log("retire generation=" + generation + "; " + reason);
        }
    }
    public void fail(String reason) {
        failed = true; active = false; available = false; stop(reason);
    }
    public void tick(long now, String record) {
        if (lastTick != 0 && now < lastTick) fail("clock rollback");
        lastTick = now;
        if (record != null) readStatus(now, record);
        if (daemon == null) return; // old binary has no protocol: no legacy fallback
        if (phase != IDLE && ((acknowledged && now - lastReply > 2000) ||
            (!acknowledged && actionSequence != 0 && now - actionTime > 3000))) fail("daemon status expired");
        if (phase == IDLE && restorePending && (restoreTime == 0 || now - restoreTime >= 1000)) {
            restoreTime = now;
            if (effects.requestOEMRestore()) {
                restorePending = false; restoreNeeded = false;
                effects.log("OEM restoration requested; physical visibility needs target confirmation");
            }
        }
        if (phase == IDLE && !restorePending && active && available && !failed) {
            if (generation == Long.MAX_VALUE) { fail("generation exhausted"); return; }
            ++generation; phase = PREPARING; action = "PREPARE";
            actionSequence = 0; acknowledged = false; restoreNeeded = true;
        }
        if (sequence == Long.MAX_VALUE) { fail("sequence exhausted"); return; }
        ++sequence;
        if (actionSequence == 0) { actionSequence = sequence; actionTime = now; }
        String line = "M2LC 1 " + daemon + " " + owner + " " + generation + " " + sequence + " " + action;
        if ("PREPARE".equals(action) && args.length() > 0) line += " " + args;
        try { effects.publish(line + "\n"); }
        catch (Exception e) { fail("command write: " + e.toString()); }
    }
    private void readStatus(long now, String record) {
        String[] w = split(record);
        if (w == null || w.length != 7 || !"M2LS".equals(w[0]) || !"1".equals(w[1]) ||
            !hex(w[2]) || !hex(w[3])) { fail("malformed status"); return; }
        long g, q;
        try { g = number(w[4]); q = number(w[5]); }
        catch (Exception e) { fail("malformed status counters"); return; }
        if (!"STOPPED".equals(w[6]) && !"PREPARED".equals(w[6]) &&
            !"RUNNING".equals(w[6]) && !"ERROR".equals(w[6])) { fail("unknown status"); return; }
        if (daemon == null || !daemon.equals(w[2])) {
            boolean hadDaemon = daemon != null;
            daemon = w[2]; sequence = 0; replySequence = 0; actionSequence = 0;
            phase = STOPPING; action = "STOP"; acknowledged = false;
            if (hadDaemon) { failed = true; available = false; effects.log("daemon replaced; fresh activation required"); }
            return; // claim with STOP, never trust unowned startup status
        }
        if (!owner.equals(w[3]) || g != generation || q < actionSequence || q > sequence || q <= replySequence) return;
        replySequence = q; lastReply = now; acknowledged = true;
        if ("ERROR".equals(w[6])) { fail("native release/start failed"); return; }
        if (phase == STOPPING && "STOPPED".equals(w[6])) {
            phase = IDLE; restorePending = restoreNeeded; restoreTime = 0;
            effects.log("player exit acknowledged generation=" + generation);
        } else if (phase == PREPARING && "PREPARED".equals(w[6])) {
            phase = RUNNING; action = "RENDER"; actionSequence = 0; acknowledged = false;
            effects.log("prepared process acknowledged; request rendering");
        } else if ((phase == PREPARING || phase == RUNNING) && "STOPPED".equals(w[6])) {
            fail("player exited");
        }
    }
    public int phase() { return phase; }
    public long generation() { return generation; }
    public boolean isActive() { return active && !failed; }
    public boolean restorationPending() { return restorePending || phase == STOPPING; }
    private static boolean hex(String s) {
        if (s == null || s.length() != 32) return false;
        for (int i = 0; i < 32; ++i) {
            char c = s.charAt(i);
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
        }
        return true;
    }
    private static long number(String s) {
        if (s.length() == 0) throw new IllegalArgumentException();
        for (int i = 0; i < s.length(); ++i) if (s.charAt(i) < '0' || s.charAt(i) > '9') throw new IllegalArgumentException();
        return Long.parseLong(s);
    }
    private static String[] split(String record) {
        if (record.length() > 256 || !record.endsWith("\n")) return null;
        String s = record.substring(0, record.length()-1);
        java.util.Vector v = new java.util.Vector();
        int start = 0;
        for (int i = 0; i <= s.length(); ++i) {
            if (i == s.length() || s.charAt(i) == ' ') {
                if (start == i) return null;
                v.addElement(s.substring(start, i)); start = i + 1;
            } else if (s.charAt(i) < 33 || s.charAt(i) > 126) return null;
        }
        String[] out = new String[v.size()]; v.copyInto(out); return out;
    }
}
