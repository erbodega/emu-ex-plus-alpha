#pragma once

#include <jni.h>

namespace Input
{

// JNI callbacks
jboolean JNICALL touchEvent(JNIEnv *env, jobject thiz, jint action, jint x, jint y, jint pid);
jboolean JNICALL trackballEvent(JNIEnv *env, jobject thiz, jint action, jfloat x, jfloat y);
jboolean JNICALL keyEvent(JNIEnv *env, jobject thiz, jint key, jint down);

// dlsym extra functions from supplied libandroid.so
bool dlLoadAndroidFuncs(void *libandroid);

}