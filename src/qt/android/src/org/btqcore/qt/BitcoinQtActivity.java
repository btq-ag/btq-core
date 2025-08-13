package org.btqcore.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class BTQQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File btqDir = new File(getFilesDir().getAbsolutePath() + "/.btq");
        if (!btqDir.exists()) {
            btqDir.mkdir();
        }

        super.onCreate(savedInstanceState);
    }
}
