package com.britzl.defold.clipboard;


import android.util.Log;
import android.app.Activity;
import android.content.Context;
import android.content.ClipData;
import java.lang.Runnable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.Callable;
import java.lang.Exception;

class ClipboardExtension {
	private static final String TAG = "clipboard";

	public static void ToClipboard(final Activity activity, final String text) {
		// must run on UI thread or getSystemService() will not return (and there's no crash either)
		activity.runOnUiThread(new Runnable() {
			@Override
			public void run() {
				if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.HONEYCOMB) {
					android.content.ClipboardManager clipboard = (android.content.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
					ClipData clip = ClipData.newPlainText("Simple text", text);
					clipboard.setPrimaryClip(clip);
				}
				else {
					android.text.ClipboardManager clipboard = (android.text.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
					clipboard.setText(text);
				}
			}
		});
	}

	public static String FromClipboard(final Activity activity) {
		FutureTask<String> futureResult = new FutureTask<String>(new Callable<String>() {
			@Override
			public String call() throws Exception {
				String text = "";
				if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.HONEYCOMB) {
					android.content.ClipboardManager clipboard = (android.content.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
					if (clipboard.hasPrimaryClip() && clipboard.getPrimaryClipDescription().hasMimeType(android.content.ClipDescription.MIMETYPE_TEXT_PLAIN)) {
						ClipData.Item item = clipboard.getPrimaryClip().getItemAt(0);
						text = item.getText().toString();
						if(text == null) {
							text = "";
						}
					}
				}
				else {
					android.text.ClipboardManager clipboard = (android.text.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
					text = clipboard.getText().toString();
				}
				return text;
			}
		});

		activity.runOnUiThread(futureResult);
		try {
			return futureResult.get();
		}
		catch(Exception e) {
			Log.w(TAG, "Unable to get text from clipboard");
			return "";
		}
	}
}
