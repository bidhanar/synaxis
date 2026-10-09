# Called from the web UI by name through addJavascriptInterface.
-keepclassmembers class io.synaxis.app.Bridge {
    @android.webkit.JavascriptInterface <methods>;
}
