/* Local P2873 lifecycle adaptation. CC BY-NC-SA 4.0; see LICENSE.md. */
package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.security.SecureRandom;

public final class ClusterLifecycleFiles {
    public static final String REQUEST = "/tmp/mib2p_cluster_request";
    public static final String STATUS = "/tmp/mib2p_cluster_status";
    private ClusterLifecycleFiles() { }
    public static String identity() {
        byte[] bytes = new byte[16]; new SecureRandom().nextBytes(bytes);
        StringBuffer out = new StringBuffer(32);
        for (int i = 0; i < bytes.length; ++i) {
            int value = bytes[i] & 255;
            out.append("0123456789abcdef".charAt(value >> 4));
            out.append("0123456789abcdef".charAt(value & 15));
        }
        return out.toString();
    }
    public static void publish(String path, String record, String owner) throws IOException {
        byte[] data = record.getBytes("US-ASCII");
        if (data.length >= 512 || !record.endsWith("\n")) throw new IOException("invalid record");
        File temporary = new File(path + "." + owner + ".new");
        FileOutputStream stream = new FileOutputStream(temporary);
        try { stream.write(data); stream.flush(); }
        finally { stream.close(); }
        if (!temporary.renameTo(new File(path))) {
            temporary.delete(); throw new IOException("atomic command rename failed");
        }
    }
    public static String read(String path) throws IOException {
        File file = new File(path);
        if (!file.exists()) return null;
        FileInputStream stream = new FileInputStream(file);
        try {
            byte[] data = new byte[257]; int size = 0, count;
            while (size < data.length && (count = stream.read(data, size, data.length - size)) > 0) size += count;
            if (size == 0 || size > 256 || data[size-1] != 10) throw new IOException("invalid status size/terminator");
            for (int i = 0; i < size-1; ++i) if (data[i] < 32 || data[i] > 126) throw new IOException("invalid status byte");
            return new String(data, 0, size, "US-ASCII");
        } finally { stream.close(); }
    }
}
