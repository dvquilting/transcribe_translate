# The native engine calls these by name (jni_bridge.cpp).
-keep class com.davidv.transcribe.engine.NativeEngine { *; }
-keep interface com.davidv.transcribe.engine.EngineListener { *; }
-keep class * implements com.davidv.transcribe.engine.EngineListener { *; }
