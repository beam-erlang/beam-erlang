// SPDX-License-Identifier: Apache-2.0
package org.erlangsters.acceptance.android;
import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
public final class AcceptanceActivity extends Activity {
    private TextView result;
    private AcceptanceApplication controller;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);controller=(AcceptanceApplication)getApplication();
        LinearLayout layout=new LinearLayout(this);layout.setOrientation(LinearLayout.VERTICAL);
        result=new TextView(this);result.setContentDescription("acceptance-result");
        Button restart=new Button(this);restart.setAllCaps(false);restart.setText("Restart VM");restart.setOnClickListener(v->controller.restart());layout.addView(restart);
        Button stress=new Button(this);stress.setAllCaps(false);stress.setText("Java GC and exception");stress.setOnClickListener(v->controller.stressJava());layout.addView(stress);
        Button recreate=new Button(this);recreate.setAllCaps(false);recreate.setText("Recreate Activity");recreate.setOnClickListener(v->recreate());layout.addView(recreate);
        Button snapshot=new Button(this);snapshot.setAllCaps(false);snapshot.setText("Snapshot");snapshot.setOnClickListener(v->controller.snapshot());layout.addView(snapshot);
        android.widget.ScrollView scroll=new android.widget.ScrollView(this);scroll.addView(result);layout.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        // Target SDK 36 uses edge-to-edge windows. Keep every control outside
        // system bars and cutouts, including on the API 26 baseline.
        layout.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        setContentView(layout);layout.requestApplyInsets();controller.attach(this);
    }
    void display(String report) { result.setText(report); }
    @Override public void onDestroy() { controller.detach(this);super.onDestroy(); }
}
