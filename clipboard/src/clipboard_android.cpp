#include <dmsdk/sdk.h>
#include "clipboard_private.h"

#if defined(DM_PLATFORM_ANDROID)


static JNIEnv* Attach()
{
	JNIEnv* env;
	JavaVM* vm = dmGraphics::GetNativeAndroidJavaVM();
	vm->AttachCurrentThread(&env, NULL);
	return env;
}

static bool Detach(JNIEnv* env)
{
	bool exception = (bool) env->ExceptionCheck();
	env->ExceptionClear();
	JavaVM* vm = dmGraphics::GetNativeAndroidJavaVM();
	vm->DetachCurrentThread();
	return !exception;
}

namespace {
	struct AttachScope {
		JNIEnv* m_Env;
		AttachScope() : m_Env(Attach()) {
		}
		~AttachScope() {
			Detach(m_Env);
		}
	};
}

static jclass GetClass(JNIEnv* env, const char* classname)
{
	jclass activity_class = env->FindClass("android/app/NativeActivity");
	jmethodID get_class_loader = env->GetMethodID(activity_class,"getClassLoader", "()Ljava/lang/ClassLoader;");
	jobject cls = env->CallObjectMethod(dmGraphics::GetNativeAndroidActivity(), get_class_loader);
	jclass class_loader = env->FindClass("java/lang/ClassLoader");
	jmethodID find_class = env->GetMethodID(class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

	jstring str_class_name = env->NewStringUTF(classname);
	jclass outcls = (jclass)env->CallObjectMethod(cls, find_class, str_class_name);
	env->DeleteLocalRef(str_class_name);
	return outcls;
}


void clipboard_to_clipboard(const char* text) {
	// prepare JNI
	AttachScope attachscope;
	JNIEnv* env = attachscope.m_Env;
	jclass cls = GetClass(env, "com.britzl.defold.clipboard.ClipboardExtension");

	// call method
	jmethodID to_clipboard = env->GetStaticMethodID(cls, "ToClipboard", "(Landroid/app/Activity;Ljava/lang/String;)V");
	env->CallStaticVoidMethod(cls, to_clipboard, dmGraphics::GetNativeAndroidActivity(), env->NewStringUTF(text));
}

const char* clipboard_from_clipboard() {
	// prepare JNI
	AttachScope attachscope;
	JNIEnv* env = attachscope.m_Env;
	jclass cls = GetClass(env, "com.britzl.defold.clipboard.ClipboardExtension");

	// call method
	jmethodID from_clipboard = env->GetStaticMethodID(cls, "FromClipboard", "(Landroid/app/Activity;)Ljava/lang/String;");
	jstring s = (jstring)env->CallStaticObjectMethod(cls, from_clipboard, dmGraphics::GetNativeAndroidActivity());

	const char *str = env->GetStringUTFChars(s, 0);
	return str;
}

#endif
