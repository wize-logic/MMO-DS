/*
 * The whole of the Java in this project, and every line of it exists for one
 * reason: a framework object with no C entry point. Android's document
 * picker is an Intent, and an Intent's answer is delivered to
 * onActivityResult, which NativeActivity swallows. There is no way to hear
 * it from C, so this subclass hears it and writes the URI where the C side is
 * watching. Nothing else lives here and nothing else may: the client is C,
 * and this is a doorbell, not a door.
 *
 * The picked URI travels as a FILE (picked.uri in the app's own directory)
 * rather than a JNI callback, so the C side needs no RegisterNatives and no
 * threading care: it polls for the file on its own thread, which it was
 * already doing every frame for everything else.
 *
 * Three jobs of that same shape live here now:
 *   - the document picker, above;
 *   - hiding the system bars, because the call that does it on a modern
 *     Android is a window controller (fullscreen() below);
 *   - handing a downloaded APK to the package installer, because the
 *     installer is a session object and its verdict comes back as an Intent
 *     (installApk() below). The C side fetched and proved the file; this
 *     only carries it across and reports what the system said, to a file the
 *     C side polls (update.result), exactly like the picker.
 */
package org.openmmo.handheld;

public class OpenMMOActivity extends android.app.NativeActivity {
    private static final int PICK_ROM = 41;
    private static final int INSTALL_PERM = 42;
    private static final String INSTALL_STATUS =
        "org.openmmo.handheld.INSTALL_STATUS";
    /* The APK waiting on the player's permission, so the install can carry
     * on by itself when they come back from the settings page. */
    private String pendingApk;

    /*
     * TAKING THE WHOLE PANEL, which used to be one line of C and no longer
     * can be.
     *
     * android_main asks for AWINDOW_FLAG_FULLSCREEN, and that was enough
     * while the app targeted 28: it hid the status and navigation bars and
     * the surface was the whole glass. FLAG_FULLSCREEN was deprecated in API
     * 30 and is IGNORED outright for an app targeting 35, which this one now
     * does, and 35 also makes every window edge-to-edge, so the bars are
     * not merely back, they are composited ON TOP of the game. The C flag is
     * left where it is because it is still the whole answer on API 26-29,
     * where the controller below does not exist.
     *
     * BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE is the sticky-immersive one: a
     * swipe from an edge shows the bars for a moment and they leave again on
     * their own. Anything else and the first accidental swipe, on a
     * handheld, that is a thumb resting on the glass, permanently gives up
     * the bottom of the screen.
     *
     * Held in a nested class so the verifier never has to resolve an API 30
     * type on a device that has none.
     */
    private static final class Bars {
        static void hide(android.view.Window w) {
            android.view.WindowInsetsController c = w.getInsetsController();

            w.setDecorFitsSystemWindows(false);
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(android.view.WindowInsetsController
                    .BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    private void fullscreen() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            Bars.hide(getWindow());
        }
    }

    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        fullscreen();
    }

    /* The bars come back on their own after a dialog, the document picker,
     * or a swipe, and the surface is resized under the game each time they
     * do. Re-asking on every focus gain is what makes it stay put. */
    @Override public void onWindowFocusChanged(boolean has) {
        super.onWindowFocusChanged(has);
        if (has) {
            fullscreen();
        }
    }

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

    /* Open a page in whatever browses on this device, the update
     * notice's last resort when the installer will not take the package, and
     * that door is an Intent too. Fire and forget, no result to hear, so no
     * request code and no listener. */
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

    /* ------------------------------------------------------------------
     * The installer.
     *
     * HOW AN APP UPDATES ITSELF ON ANDROID: it does not. The package
     * installer does, on the app's request, after the player confirms in a
     * dialog the system owns. What the app supplies is the bytes and the
     * request; what it gets back is a verdict. PackageInstaller's session
     * API is used rather than a content:// URI handed to ACTION_VIEW because
     * the session takes the bytes from THIS process, no provider to
     * declare, no path another app has to be able to read, and the file the
     * C side proved is the file that is streamed.
     *
     * THE ONE PERMISSION, REQUEST_INSTALL_PACKAGES, is a per-app switch the
     * player flips once in a settings page Android opens for the purpose;
     * there is no runtime dialog for it. Asked here only when it is missing,
     * and the install resumes on its own when the player comes back with it
     * granted (onActivityResult, INSTALL_PERM).
     *
     * Called from C over JNI, on the C side's worker thread; nothing here
     * needs the UI thread until an activity is started, and those hops are
     * posted.
     * ------------------------------------------------------------------ */

    public void installApk(String path) {
        pendingApk = path;
        if (!getPackageManager().canRequestPackageInstalls()) {
            final OpenMMOActivity self = this;
            runOnUiThread(new Runnable() {
                public void run() {
                    try {
                        self.startActivityForResult(new android.content.Intent(
                            android.provider.Settings
                                .ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            android.net.Uri.parse("package:"
                                                  + self.getPackageName())),
                            INSTALL_PERM);
                        self.result("denied");
                    } catch (Exception e) {
                        self.result("failed this device has no page on which"
                                    + " to allow the install");
                    }
                }
            });
            return;
        }
        commit(path);
    }

    /* The session: create, stream the file in, commit with an Intent back to
     * this activity. The bytes are copied rather than the path handed over
     * because that is the API; the verdict arrives at onNewIntent. */
    private void commit(String path) {
        try {
            android.content.pm.PackageInstaller pi =
                getPackageManager().getPackageInstaller();
            android.content.pm.PackageInstaller.SessionParams p =
                new android.content.pm.PackageInstaller.SessionParams(
                    android.content.pm.PackageInstaller.SessionParams
                        .MODE_FULL_INSTALL);
            java.io.File f = new java.io.File(path);
            p.setAppPackageName(getPackageName());
            p.setSize(f.length());
            int id = pi.createSession(p);
            android.content.pm.PackageInstaller.Session s = pi.openSession(id);
            java.io.InputStream in = new java.io.FileInputStream(f);
            java.io.OutputStream out = s.openWrite("openmmo.apk", 0, f.length());
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            s.fsync(out);
            out.close();
            in.close();
            android.content.Intent i = new android.content.Intent(
                this, OpenMMOActivity.class);
            i.setAction(INSTALL_STATUS);
            i.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK
                       | android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP);
            /* MUTABLE, because the installer fills the status into this very
             * Intent; an immutable one (the default from 31) comes back with
             * no verdict in it. FLAG_MUTABLE itself is a 31 constant, inlined
             * by javac, so the branch is on the running version. */
            int flags = android.app.PendingIntent.FLAG_UPDATE_CURRENT;
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                flags |= android.app.PendingIntent.FLAG_MUTABLE;
            }
            android.app.PendingIntent pend =
                android.app.PendingIntent.getActivity(this, id, i, flags);
            s.commit(pend.getIntentSender());
            s.close();
        } catch (Exception e) {
            result("failed " + describe(e));
        }
    }

    private static String describe(Exception e) {
        String m = e.getMessage();
        return m == null || m.isEmpty() ? e.getClass().getSimpleName() : m;
    }

    /* The installer's verdict, delivered as an Intent to this singleTask
     * activity. Pending-user-action is the normal path: the system's own
     * confirmation dialog is handed over to be shown, and the player's answer
     * comes back through here again as success (the process is replaced and
     * never sees it) or a failure with a reason. */
    @Override protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        if (intent == null || !INSTALL_STATUS.equals(intent.getAction())) {
            return;
        }
        int st = intent.getIntExtra(
            android.content.pm.PackageInstaller.EXTRA_STATUS,
            android.content.pm.PackageInstaller.STATUS_FAILURE);
        if (st == android.content.pm.PackageInstaller
                .STATUS_PENDING_USER_ACTION) {
            android.content.Intent confirm = (android.content.Intent)
                intent.getParcelableExtra(android.content.Intent.EXTRA_INTENT);
            try {
                if (confirm == null) {
                    throw new IllegalStateException("no dialog to show");
                }
                startActivity(confirm);
                result("pending");
            } catch (Exception e) {
                result("failed the system's install dialog could not be"
                       + " opened: " + describe(e));
            }
            return;
        }
        if (st == android.content.pm.PackageInstaller.STATUS_SUCCESS) {
            result("done");
            return;
        }
        String why = intent.getStringExtra(
            android.content.pm.PackageInstaller.EXTRA_STATUS_MESSAGE);
        result("failed " + reason(st, why));
    }

    /* The status codes, as sentences a player can act on. The system's own
     * message is appended when it has one, since it names the actual cause
     * (a signature that does not match the installed app, say). */
    private static String reason(int st, String why) {
        String s;
        switch (st) {
        case android.content.pm.PackageInstaller.STATUS_FAILURE_ABORTED:
            s = "the install was cancelled"; break;
        case android.content.pm.PackageInstaller.STATUS_FAILURE_BLOCKED:
            s = "the install was blocked on this device"; break;
        case android.content.pm.PackageInstaller.STATUS_FAILURE_CONFLICT:
            s = "the new build conflicts with the installed one"; break;
        case android.content.pm.PackageInstaller.STATUS_FAILURE_INCOMPATIBLE:
            s = "the new build is not compatible with this device"; break;
        case android.content.pm.PackageInstaller.STATUS_FAILURE_INVALID:
            s = "the downloaded package is not a valid app"; break;
        case android.content.pm.PackageInstaller.STATUS_FAILURE_STORAGE:
            s = "there is not enough storage for the install"; break;
        default:
            s = "the installer refused (" + st + ")"; break;
        }
        if (why != null && !why.isEmpty()) {
            s = s + ": " + why;
        }
        return s;
    }

    /* One line, atomically renamed into place, so the C side never reads
     * half a verdict. See deliver() for the picker's identical arrangement;
     * this one is under the app's PRIVATE directory because the package
     * beside it is too. */
    private void result(String line) {
        try {
            java.io.File dir = getFilesDir();
            java.io.File tmp = new java.io.File(dir, "update.result.part");
            java.io.FileOutputStream f = new java.io.FileOutputStream(tmp);
            f.write(line.getBytes("UTF-8"));
            f.close();
            tmp.renameTo(new java.io.File(dir, "update.result"));
        } catch (Exception e) { }
    }

    @Override protected void onActivityResult(int req, int res,
                                              android.content.Intent data) {
        if (req == INSTALL_PERM) {
            /* Back from the settings page. Granted means the install the
             * player asked for carries on without a second press; not
             * granted is said as such, and the panel's own button asks
             * again. */
            final String apk = pendingApk;
            if (apk != null && getPackageManager().canRequestPackageInstalls()) {
                new Thread(new Runnable() {
                    public void run() { commit(apk); }
                }).start();
            } else {
                result("failed OpenMMO was not allowed to install apps");
            }
            return;
        }
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
