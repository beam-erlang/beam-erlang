// SPDX-License-Identifier: Apache-2.0
package org.erlangsters.acceptance.android;

import android.app.Activity;
import android.app.Application;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import java.io.File;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.json.JSONObject;

public final class AcceptanceApplication extends Application implements Application.ActivityLifecycleCallbacks {
    static { System.loadLibrary("beam_acceptance"); }
    private static native String nativeAbi();
    private static native String[] nativeImageNames();
    private native void nativeRun(AssetManager assets, String directory, String[] images);
    private static native void nativeCommand(int command);
    private final Handler main = new Handler(Looper.getMainLooper());
    private WeakReference<AcceptanceActivity> ui = new WeakReference<>(null);
    private volatile String report = "{\"state\":\"initializing\"}";
    private int started;
    private long heartbeats, javaExceptions, activityCreations, backgrounds;
    private int stressRemaining;
    private final Runnable idle = new Runnable() {
        public void run() { heartbeats++; main.postDelayed(this, 20); }
    };
    @Override public void onCreate() {
        super.onCreate(); registerActivityLifecycleCallbacks(this); main.post(idle);
        new Thread(() -> {
            try { nativeRun(getAssets(), getFilesDir().getAbsolutePath(), selectedImages()); }
            catch (Throwable failure) { onNativeReport("{\"state\":\"failed\",\"error\":"+JSONObject.quote(failure.toString())+"}"); }
        }, "BEAM owner").start();
    }
    // Native calls enter on the owner OS thread; snapshots enter on the UI thread.
    // Publishing always queues UI work without blocking the native owner.
    private void onNativeReport(String value) {
        main.post(() -> {
            try {
                JSONObject record = new JSONObject(value);
                record.put("ui_heartbeats", heartbeats); record.put("java_exceptions", javaExceptions);
                record.put("api", android.os.Build.VERSION.SDK_INT);
                record.put("process_id", android.os.Process.myPid());
                record.put("activity_creations", activityCreations); record.put("backgrounds", backgrounds);
                record.put("debugger_attached", android.os.Debug.isDebuggerConnected());
                record.put("debuggable", (getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0);
                report = record.toString();
            } catch (Exception failure) { report = "{\"state\":\"failed\",\"error\":"+JSONObject.quote(failure.toString())+"}"; }
            android.util.Log.i("BEAMAcceptance", report);
            AcceptanceActivity activity = ui.get(); if (activity != null) activity.display(report);
        });
    }
    private String[] selectedImages() throws Exception {
        String abi = nativeAbi();
        List<String> archives = new ArrayList<>(); archives.add(getApplicationInfo().sourceDir);
        if (getApplicationInfo().splitSourceDirs != null)
            java.util.Collections.addAll(archives, getApplicationInfo().splitSourceDirs);
        String[] names = nativeImageNames();
        if (BuildConfig.INVALID_NATIVE_SELECTION) names[0] = "libbeam-invalid-selection.so";
        String[] selected = new String[names.length];
        for (int i=0; i<names.length; i++) {
            String member = "lib/"+abi+"/"+names[i];
            for (String archive : archives) {
                try (ZipFile zip = new ZipFile(archive)) {
                    ZipEntry entry = zip.getEntry(member);
                    if (entry != null) {
                        if (selected[i] != null || entry.getMethod() != ZipEntry.STORED)
                            throw new IllegalStateException("Ambiguous or compressed native image: "+member);
                        selected[i] = new File(archive).getCanonicalPath()+"!/"+member;
                    }
                }
            }
            if (selected[i] == null) throw new IllegalStateException("Missing native image: "+member);
        }
        return selected;
    }
    void attach(AcceptanceActivity activity) { ui = new WeakReference<>(activity); activity.display(report); }
    void detach(AcceptanceActivity activity) { if (ui.get() == activity) ui.clear(); }
    void restart() { nativeCommand(2); }
    void snapshot() { onNativeReport(report); }
    void stressJava() { stressRemaining=40; nativeCommand(2); stressStep(); }
    private void stressStep() {
        // ART collection and Java exceptions must coexist with active BEAM workers.
        byte[][] garbage = new byte[16][]; for (int i=0;i<garbage.length;i++) garbage[i]=new byte[128*1024];
        System.gc(); try { throw new IllegalStateException("qualification exception"); } catch (IllegalStateException expected) { javaExceptions++; }
        if (--stressRemaining > 0) main.postDelayed(this::stressStep, 20);
    }
    @Override public void onActivityStarted(Activity activity) { started++; nativeCommand(1); }
    @Override public void onActivityStopped(Activity activity) {
        started--; if (started == 0 && !activity.isChangingConfigurations()) { backgrounds++; nativeCommand(0); }
    }
    @Override public void onActivityCreated(Activity a, Bundle b) { activityCreations++; }
    @Override public void onActivityResumed(Activity a) {}
    @Override public void onActivityPaused(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    @Override public void onActivityDestroyed(Activity a) {}
}
