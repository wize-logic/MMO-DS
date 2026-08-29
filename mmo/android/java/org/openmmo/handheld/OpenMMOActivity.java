/*
 * The whole of the Java in this project, and every line of it exists for one
 * reason: Android's document picker is an Intent, and an Intent's answer is
 * delivered to onActivityResult, which NativeActivity swallows. There is no
 * way to hear it from C, so this subclass hears it and writes the URI where
 * the C side is watching. Nothing else lives here and nothing else may: the
 * client is C, and this is a doorbell, not a door.
 *
 * The picked URI travels as a FILE (picked.uri in the app's own directory)
 * rather than a JNI callback, so the C side needs no RegisterNatives and no
 * threading care: it polls for the file on its own thread, which it was
 * already doing every frame for everything else.
 */
package org.openmmo.handheld;

public class OpenMMOActivity extends android.app.NativeActivity {
    private static final int PICK_ROM = 41;

    /* Called from C over JNI when the player asks to choose a cartridge. */
    public void pickRom() {
        final OpenMMOActivity self = this;
        runOnUiThread(new Runnable() {
            public void run() {
                try {
                    android.content.Intent i = new android.content.Intent(
                        android.content.Intent.ACTION_OPEN_DOCUMENT);
                    i.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                    /* A .nds has no MIME type of its own; offering all files
                     * is what lets the player see theirs. */
                    i.setType("*/*");
                    self.startActivityForResult(i, PICK_ROM);
                } catch (Exception e) {
                    self.deliver("");   /* no picker on this device: C falls
                                           back to its own browser */
                }
            }
        });
    }

    /* Open a page in whatever browses on this device. For the update
     * notice: the app never rewrites itself, so "update" means taking the
     * player to the releases page, and that door is an Intent too. Fire and
     * forget, no result to hear, so no request code and no listener. */
    public void openUrl(final String url) {
        final OpenMMOActivity self = this;
        runOnUiThread(new Runnable() {
            public void run() {
                try {
                    self.startActivity(new android.content.Intent(
                        android.content.Intent.ACTION_VIEW,
                        android.net.Uri.parse(url)));
                } catch (Exception e) { /* no browser is a device this app
                                           cannot help; the C side already
                                           logged the ask */ }
            }
        });
    }

    /* Can the URI be read right now? The C side asks this to keep the
     * launcher's footer honest after the file behind it is deleted. */
    public boolean romOk(String uri) {
        try {
            android.os.ParcelFileDescriptor p = getContentResolver()
                .openFileDescriptor(android.net.Uri.parse(uri), "r");
            p.close();
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /* The URI as a real file descriptor the engine can boot from, detached
     * so its life belongs to the C side. -1 when it cannot be opened. */
    public int romFd(String uri) {
        try {
            android.os.ParcelFileDescriptor p = getContentResolver()
                .openFileDescriptor(android.net.Uri.parse(uri), "r");
            return p.detachFd();
        } catch (Exception e) {
            return -1;
        }
    }

    @Override protected void onActivityResult(int req, int res,
                                              android.content.Intent data) {
        if (req != PICK_ROM) {
            super.onActivityResult(req, res, data);
            return;
        }
        String out = "";
        if (res == RESULT_OK && data != null && data.getData() != null) {
            android.net.Uri u = data.getData();
            try {
                /* Keep the grant across reboots, or the cartridge would be
                 * readable until the first power cycle and then not. */
                getContentResolver().takePersistableUriPermission(u,
                    android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception e) { /* non-persistable providers still work
                                       for this session */ }
            out = u.toString();
        }
        deliver(out);
    }

    /* One file, atomically renamed into place, so the C side never reads a
     * half-written URI. An empty file means "cancelled". */
    private void deliver(String uri) {
        try {
            java.io.File dir = getExternalFilesDir(null);
            java.io.File tmp = new java.io.File(dir, "picked.uri.part");
            java.io.FileOutputStream f = new java.io.FileOutputStream(tmp);
            f.write(uri.getBytes("UTF-8"));
            f.close();
            tmp.renameTo(new java.io.File(dir, "picked.uri"));
        } catch (Exception e) { }
    }
}
