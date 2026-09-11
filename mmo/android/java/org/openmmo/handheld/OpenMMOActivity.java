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
 * Four jobs of that same shape live here now:
 *   - the document picker, above;
 *   - hiding the system bars, because the call that does it on a modern
 *     Android is a window controller (fullscreen() below);
 *   - handing a downloaded APK to the package installer, because the
 *     installer is a session object and its verdict comes back as an Intent
 *     (installApk() below). The C side fetched and proved the file; this
 *     only carries it across and reports what the system said, to a file the
 *     C side polls (update.result), exactly like the picker;
 *   - the keyboard. The system keyboard types into an InputConnection, an
 *     object the framework asks the focused view for and the NDK cannot
 *     supply, so ImeView below supplies one and writes what is typed down a
 *     pipe the C side made and reads (imeAttach / imeShow, at the end).
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
        /* The keyboard's view, over the native one. It draws nothing and
         * takes no touch (the C side answers every pointer at the input
         * queue, ahead of any view); it exists to hold focus and answer
         * onCreateInputConnection. */
        imeView = new ImeView(this);
        addContentView(imeView, new android.view.ViewGroup.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        imeView.requestFocus();
        /* RAISED BY THE GAME, NEVER BY THE WINDOW. NativeActivity's own
         * onCreate sets the window to STATE_UNSPECIFIED | ADJUST_RESIZE,
         * over anything the manifest says, and with a focused editor view
         * in it UNSPECIFIED means the framework raises the keyboard itself
         * the moment the window gains focus, measured: it came up over
         * the front door with no field open, the first time a dialog
         * closed. So the mode is set again here, after super: the keyboard
         * only ever comes when a field opens, and the surface is never
         * resized under the game for it (the window lays itself out above
         * the keyboard from the rows it is told it covers). */
        getWindow().setSoftInputMode(
            android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
            | android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
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

    /* ------------------------------------------------------------------
     * The keyboard.
     *
     * THE SYSTEM KEYBOARD TYPES INTO AN INPUTCONNECTION, and that object is
     * the reason this is Java: the framework asks the focused view for one
     * and feeds it commitText, setComposingText, deleteSurroundingText and
     * the rest, and the NDK has no way to hand one over. Without one an IME
     * falls back to key events, which carry a keycode and lose everything
     * else, a word swiped, a suggestion taken, any character that is not
     * a key on a US layout.
     *
     * WHAT CROSSES TO C IS BYTES DOWN A PIPE, not a JNI callback. The C side
     * makes the pipe, hands the write end over (imeAttach) and reads the
     * other end on the looper it already runs, so there is no
     * RegisterNatives, no JNI_OnLoad and no thread the C side did not have.
     * The stream is UTF-8 text with a few control bytes in it:
     *
     *   0x08 backspace    0x7F delete       0x0A the SEND key
     *   0x11 caret left   0x12 caret right  0x01 home   0x05 end
     *   0x0E then four bytes, big-endian: the rows of glass the keyboard
     *        covers, 0 once it has gone
     *
     * THE MIRROR. The connection keeps its own copy of the line, the
     * Editable the base class edits, so the keyboard can read what is
     * before the caret and take back a word it committed. Every edit is then
     * sent as the difference against what C was last told: delete this many,
     * insert these. An edit that reaches the end of the line is anchored
     * with END rather than counted from the last caret, so a field that
     * quietly capped the line, or was typed into by the pen as well, still
     * deletes the character the player sees and appends where they expect.
     *
     * The line is empty every time the C side raises the keyboard: a field
     * that opens is a fresh field, and a name box and a password box that
     * open one after the other must not carry the name into the password.
     * ------------------------------------------------------------------ */

    private static final int IME_BS = 0x08, IME_DEL = 0x7F, IME_SEND = 0x0A;
    private static final int IME_LEFT = 0x11, IME_RIGHT = 0x12;
    private static final int IME_HOME = 0x01, IME_END = 0x05, IME_INSET = 0x0E;

    private ImeView imeView;
    private android.os.ParcelFileDescriptor imePfd;
    private java.io.FileOutputStream imeOut;

    /* The write end of the C side's pipe, adopted: closing is the
     * descriptor's own, when this object goes. Called over JNI, once. */
    public synchronized void imeAttach(int fd) {
        try {
            imePfd = android.os.ParcelFileDescriptor.adoptFd(fd);
            imeOut = new java.io.FileOutputStream(imePfd.getFileDescriptor());
        } catch (Exception e) {
            imeOut = null;
        }
    }

    private synchronized void imePut(byte[] b) {
        if (imeOut == null) {
            return;
        }
        try {
            imeOut.write(b);
        } catch (Exception e) { /* the C side has gone; nothing to type
                                   into */ }
    }

    private void imePut(int one) {
        imePut(new byte[] { (byte)one });
    }

    /* Raise or put away the keyboard. Called over JNI from whichever thread
     * opened or closed a text field; the work is posted to the UI thread,
     * which is the only one the input method manager answers. */
    public void imeShow(final boolean on) {
        runOnUiThread(new Runnable() {
            public void run() {
                android.view.inputmethod.InputMethodManager imm =
                    (android.view.inputmethod.InputMethodManager)
                        getSystemService(INPUT_METHOD_SERVICE);

                if (imeView == null || imm == null) {
                    return;
                }
                if (on) {
                    imeView.requestFocus();
                    if (imeView.conn != null) {
                        imeView.conn.reset();
                    }
                    imm.restartInput(imeView);
                    imm.showSoftInput(imeView, 0);
                } else {
                    imm.hideSoftInputFromWindow(imeView.getWindowToken(), 0);
                }
            }
        });
    }

    /* API 30's insets, in a nested class so the verifier never resolves
     * the type on an older device. */
    private static final class Insets30 {
        static int ime(android.view.WindowInsets in) {
            return in.getInsets(android.view.WindowInsets.Type.ime()).bottom;
        }
    }

    private final class ImeView extends android.view.View {
        ImeConnection conn;
        int inset;          /* rows covered, as last told to C */

        ImeView(android.content.Context c) {
            super(c);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override public android.view.inputmethod.InputConnection
        onCreateInputConnection(android.view.inputmethod.EditorInfo out) {
            if (conn == null) {
                conn = new ImeConnection(this);
            }
            /* Plain text, one line, the action key reading SEND: what the
             * drawn keyboard this replaces had on its corner key, and what
             * a chat line wants. NO_EXTRACT_UI and NO_FULLSCREEN keep a
             * landscape keyboard from covering the game with its own edit
             * box. */
            out.inputType = android.text.InputType.TYPE_CLASS_TEXT;
            out.imeOptions = android.view.inputmethod.EditorInfo.IME_ACTION_SEND
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN;
            out.initialSelStart = android.text.Selection
                .getSelectionStart(conn.text);
            out.initialSelEnd = android.text.Selection
                .getSelectionEnd(conn.text);
            if (out.initialSelStart < 0) {
                out.initialSelStart = 0;
            }
            if (out.initialSelEnd < 0) {
                out.initialSelEnd = 0;
            }
            return conn;
        }

        /* How much of the glass the keyboard covers, so the C side can lay
         * the window out above it. On 30 and later the window's insets say
         * so directly (the activity has told the decor not to fit them, so
         * they reach this view intact). Before 30 the visible display frame
         * is the classic measure, taken at every layout. */
        @Override public android.view.WindowInsets
        onApplyWindowInsets(android.view.WindowInsets in) {
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                report(Insets30.ime(in));
            }
            return super.onApplyWindowInsets(in);
        }

        @Override protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            if (android.os.Build.VERSION.SDK_INT < 30) {
                getViewTreeObserver().addOnGlobalLayoutListener(
                    new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
                        public void onGlobalLayout() {
                            android.graphics.Rect r = new android.graphics.Rect();
                            getWindowVisibleDisplayFrame(r);
                            report(Math.max(0, getRootView().getHeight()
                                               - r.bottom));
                        }
                    });
            }
        }

        void report(int px) {
            if (px < 0) {
                px = 0;
            }
            if (px == inset) {
                return;
            }
            inset = px;
            imePut(new byte[] { (byte)IME_INSET, (byte)(px >> 24),
                                (byte)(px >> 16), (byte)(px >> 8), (byte)px });
        }
    }

    private final class ImeConnection
            extends android.view.inputmethod.BaseInputConnection {
        final android.text.Editable text =
            new android.text.SpannableStringBuilder();
        String seen = "";       /* the line as C was last told it */
        int seenCaret;          /* and where its caret was */
        int batch;              /* nested batch edits: mirror at the end */

        ImeConnection(android.view.View v) {
            super(v, true);
            android.text.Selection.setSelection(text, 0);
        }

        @Override public android.text.Editable getEditable() {
            return text;
        }

        void reset() {
            text.clear();
            text.clearSpans();
            android.text.Selection.setSelection(text, 0);
            seen = "";
            seenCaret = 0;
            batch = 0;
        }

        @Override public boolean beginBatchEdit() {
            batch++;
            return true;
        }

        @Override public boolean endBatchEdit() {
            if (batch > 0 && --batch == 0) {
                mirror();
            }
            return batch > 0;
        }

        @Override public boolean commitText(CharSequence t, int pos) {
            boolean r = super.commitText(t, pos);
            mirror();
            return r;
        }

        @Override public boolean setComposingText(CharSequence t, int pos) {
            boolean r = super.setComposingText(t, pos);
            mirror();
            return r;
        }

        @Override public boolean setComposingRegion(int a, int b) {
            boolean r = super.setComposingRegion(a, b);
            mirror();
            return r;
        }

        @Override public boolean finishComposingText() {
            boolean r = super.finishComposingText();
            mirror();
            return r;
        }

        @Override public boolean deleteSurroundingText(int before, int after) {
            boolean r = super.deleteSurroundingText(before, after);
            mirror();
            return r;
        }

        @Override public boolean deleteSurroundingTextInCodePoints(int before,
                                                                   int after) {
            boolean r = super.deleteSurroundingTextInCodePoints(before, after);
            mirror();
            return r;
        }

        @Override public boolean setSelection(int a, int b) {
            boolean r = super.setSelection(a, b);
            mirror();
            return r;
        }

        /* The action key. Whatever the keyboard calls it, the field reads
         * it as the line being sent. */
        @Override public boolean performEditorAction(int action) {
            imePut(IME_SEND);
            return true;
        }

        /* The keys a keyboard still sends as keys: delete with nothing
         * before the caret, enter on a keyboard with no action key, the
         * caret keys of one that has them. Editing keys are applied to the
         * mirror and sent as its difference; a key that finds the mirror
         * empty goes down raw, because the field may still hold a line the
         * mirror never saw. Anything else is an ordinary key and goes the
         * way every key goes. */
        @Override public boolean sendKeyEvent(android.view.KeyEvent e) {
            int code = e.getKeyCode();
            int lo, hi;

            switch (code) {
            case android.view.KeyEvent.KEYCODE_DEL:
            case android.view.KeyEvent.KEYCODE_FORWARD_DEL:
            case android.view.KeyEvent.KEYCODE_ENTER:
            case android.view.KeyEvent.KEYCODE_NUMPAD_ENTER:
            case android.view.KeyEvent.KEYCODE_DPAD_LEFT:
            case android.view.KeyEvent.KEYCODE_DPAD_RIGHT:
            case android.view.KeyEvent.KEYCODE_MOVE_HOME:
            case android.view.KeyEvent.KEYCODE_MOVE_END:
                break;
            default:
                return super.sendKeyEvent(e);
            }
            if (e.getAction() != android.view.KeyEvent.ACTION_DOWN) {
                return true;
            }
            lo = android.text.Selection.getSelectionStart(text);
            hi = android.text.Selection.getSelectionEnd(text);
            if (lo < 0 || hi < 0) {
                lo = hi = text.length();
            }
            if (lo > hi) {
                int t = lo; lo = hi; hi = t;
            }
            switch (code) {
            case android.view.KeyEvent.KEYCODE_DEL:
                if (hi > lo) {
                    text.delete(lo, hi);
                } else if (lo > 0) {
                    text.delete(lo - back(lo), lo);
                } else {
                    imePut(IME_BS);
                    return true;
                }
                break;
            case android.view.KeyEvent.KEYCODE_FORWARD_DEL:
                if (hi > lo) {
                    text.delete(lo, hi);
                } else if (hi < text.length()) {
                    text.delete(hi, hi + forward(hi));
                } else {
                    imePut(IME_DEL);
                    return true;
                }
                break;
            case android.view.KeyEvent.KEYCODE_ENTER:
            case android.view.KeyEvent.KEYCODE_NUMPAD_ENTER:
                imePut(IME_SEND);
                return true;
            case android.view.KeyEvent.KEYCODE_DPAD_LEFT:
                if (lo > 0) {
                    android.text.Selection.setSelection(text, lo - back(lo));
                } else {
                    imePut(IME_LEFT);
                    return true;
                }
                break;
            case android.view.KeyEvent.KEYCODE_DPAD_RIGHT:
                if (hi < text.length()) {
                    android.text.Selection.setSelection(text, hi + forward(hi));
                } else {
                    imePut(IME_RIGHT);
                    return true;
                }
                break;
            case android.view.KeyEvent.KEYCODE_MOVE_HOME:
                android.text.Selection.setSelection(text, 0);
                break;
            case android.view.KeyEvent.KEYCODE_MOVE_END:
                android.text.Selection.setSelection(text, text.length());
                break;
            default:
                break;
            }
            mirror();
            return true;
        }

        /* One character before `at`, in UTF-16 units: two for a pair. */
        private int back(int at) {
            return at >= 2 && Character.isLowSurrogate(text.charAt(at - 1))
                && Character.isHighSurrogate(text.charAt(at - 2)) ? 2 : 1;
        }

        /* One character at `at`, likewise. */
        private int forward(int at) {
            return at + 1 < text.length()
                && Character.isHighSurrogate(text.charAt(at))
                && Character.isLowSurrogate(text.charAt(at + 1)) ? 2 : 1;
        }

        private void moves(java.io.ByteArrayOutputStream out, int from,
                                  int to) {
            while (from < to) {
                out.write(IME_RIGHT);
                from++;
            }
            while (from > to) {
                out.write(IME_LEFT);
                from--;
            }
        }

        /* Send the difference between the mirror and what C holds. */
        private void mirror() {
            String now;
            int caret, max, p, s, maxs, removed, at;
            String ins;
            java.io.ByteArrayOutputStream out;

            if (batch > 0) {
                return;
            }
            now = text.toString();
            caret = android.text.Selection.getSelectionEnd(text);
            if (caret < 0 || caret > now.length()) {
                caret = now.length();
            }
            if (now.equals(seen) && caret == seenCaret) {
                return;
            }
            max = Math.min(seen.length(), now.length());
            p = 0;
            while (p < max && seen.charAt(p) == now.charAt(p)) {
                p++;
            }
            /* Never cut a surrogate pair: a prefix ending on a high half
             * gives it back, and so does a suffix starting on a low one. */
            if (p > 0 && Character.isHighSurrogate(now.charAt(p - 1))) {
                p--;
            }
            maxs = max - p;
            s = 0;
            while (s < maxs && seen.charAt(seen.length() - 1 - s)
                                == now.charAt(now.length() - 1 - s)) {
                s++;
            }
            if (s > 0 && Character.isLowSurrogate(now.charAt(now.length() - s))) {
                s--;
            }
            removed = seen.length() - s - p;
            ins = now.substring(p, now.length() - s);
            out = new java.io.ByteArrayOutputStream();
            if (s == 0) {
                /* The edit reaches the end of the line: anchor there. */
                out.write(IME_END);
                at = seen.length();
            } else {
                at = seenCaret;
                moves(out, at, seen.length() - s);
                at = seen.length() - s;
            }
            while (removed-- > 0) {
                out.write(IME_BS);
                at--;
            }
            if (ins.length() > 0) {
                byte[] b = clean(ins).getBytes(
                    java.nio.charset.StandardCharsets.UTF_8);

                out.write(b, 0, b.length);
                at += ins.length();
            }
            moves(out, at, caret);
            seen = now;
            seenCaret = caret;
            imePut(out.toByteArray());
        }

        /* A control character in typed text would read as an edit on the
         * far side; it becomes a space, so the mirror's count still holds. */
        private String clean(String s) {
            StringBuilder b = null;
            int i;

            for (i = 0; i < s.length(); i++) {
                char c = s.charAt(i);

                if (c < 0x20 || c == 0x7F) {
                    if (b == null) {
                        b = new StringBuilder(s);
                    }
                    b.setCharAt(i, ' ');
                }
            }
            return b == null ? s : b.toString();
        }
    }
}
