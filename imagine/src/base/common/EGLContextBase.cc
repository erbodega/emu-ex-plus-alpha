/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "EGL"
#include <imagine/base/GLContext.hh>
#include <imagine/base/EGLContextBase.hh>
#include <EGL/eglext.h>
#include <imagine/util/egl.hh>
#ifdef __ANDROID__
#include <imagine/base/android/android.hh>
#endif

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x0040
#endif

namespace Base
{

static bool hasDummyPbuffConfig = false;
static EGLConfig dummyPbuffConfig{};
static EGLDisplay display = EGL_NO_DISPLAY;
using EGLAttrList = StaticArrayList<int, 24>;
using EGLContextAttrList = StaticArrayList<int, 16>;

static EGLAttrList glConfigAttrsToEGLAttrs(GLContextAttributes ctxAttr, GLBufferConfigAttributes attr)
{
	EGLAttrList list;
	// don't accept slow configs
	list.push_back(EGL_CONFIG_CAVEAT);
	list.push_back(EGL_NONE);
	switch(attr.pixelFormat().id())
	{
		bdefault:
			bug_branch("%d", attr.pixelFormat().id());
		bcase PIXEL_NONE:
			// don't set any color bits
		bcase PIXEL_RGB565:
			list.push_back(EGL_BUFFER_SIZE);
			list.push_back(16);
		bcase PIXEL_RGB888:
			list.push_back(EGL_RED_SIZE);
			list.push_back(8);
			list.push_back(EGL_GREEN_SIZE);
			list.push_back(8);
			list.push_back(EGL_BLUE_SIZE);
			list.push_back(8);
		bcase PIXEL_RGBX8888:
			list.push_back(EGL_RED_SIZE);
			list.push_back(8);
			list.push_back(EGL_GREEN_SIZE);
			list.push_back(8);
			list.push_back(EGL_BLUE_SIZE);
			list.push_back(8);
			list.push_back(EGL_BUFFER_SIZE);
			list.push_back(32);
		bcase PIXEL_RGBA8888:
			list.push_back(EGL_RED_SIZE);
			list.push_back(8);
			list.push_back(EGL_GREEN_SIZE);
			list.push_back(8);
			list.push_back(EGL_BLUE_SIZE);
			list.push_back(8);
			list.push_back(EGL_ALPHA_SIZE);
			list.push_back(8);
	}
	if(!ctxAttr.openGLESAPI())
	{
		list.push_back(EGL_RENDERABLE_TYPE);
		list.push_back(EGL_OPENGL_BIT);
		//logDMsg("using OpenGL renderable");
	}
	else if(ctxAttr.majorVersion() == 2)
	{
		list.push_back(EGL_RENDERABLE_TYPE);
		list.push_back(EGL_OPENGL_ES2_BIT);
	}
	else if(ctxAttr.majorVersion() == 3)
	{
		list.push_back(EGL_RENDERABLE_TYPE);
		list.push_back(EGL_OPENGL_ES3_BIT);
	}
	list.push_back(EGL_NONE);
	return list;
}

static EGLContextAttrList glContextAttrsToEGLAttrs(GLContextAttributes attr)
{
	EGLContextAttrList list;

	if(attr.openGLESAPI())
	{
		list.push_back(EGL_CONTEXT_CLIENT_VERSION);
		list.push_back(attr.majorVersion());
	}
	else
	{
		list.push_back(EGL_CONTEXT_MAJOR_VERSION_KHR);
		list.push_back(attr.majorVersion());
		list.push_back(EGL_CONTEXT_MINOR_VERSION_KHR);
		list.push_back(attr.minorVersion());

		if(attr.majorVersion() > 3
			|| (attr.majorVersion() == 3 && attr.minorVersion() >= 2))
		{
			list.push_back(EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR);
			list.push_back(EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);
		}
	}

	if(attr.debug())
	{
		list.push_back(EGL_CONTEXT_FLAGS_KHR);
		list.push_back(EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR);
	}

	list.push_back(EGL_NONE);
	return list;
}

std::pair<bool, EGLConfig> EGLContextBase::chooseConfig(GLContextAttributes ctxAttr, GLBufferConfigAttributes attr)
{
	if(eglDisplay() == EGL_NO_DISPLAY)
	{
		logErr("unable to get EGL display");
		return std::make_pair(false, EGLConfig{});
	}
	EGLConfig config;
	EGLint configs = 0;
	{
		auto eglAttr = glConfigAttrsToEGLAttrs(ctxAttr, attr);
		eglChooseConfig(display, &eglAttr[0], &config, 1, &configs);
	}
	if(!configs && attr.pixelFormat() != Window::defaultPixelFormat())
	{
		logErr("no EGL configs found, retrying with default window format");
		attr.setPixelFormat(Window::defaultPixelFormat());
		auto eglAttr = glConfigAttrsToEGLAttrs(ctxAttr, attr);
		eglChooseConfig(display, &eglAttr[0], &config, 1, &configs);
	}
	if(!configs)
	{
		logErr("no EGL configs found, retrying with no color bits set");
		attr.setPixelFormat(IG::PIXEL_NONE);
		auto eglAttr = glConfigAttrsToEGLAttrs(ctxAttr, attr);
		eglChooseConfig(display, &eglAttr[0], &config, 1, &configs);
	}
	if(!configs)
	{
		logErr("no usable EGL configs found with major version:%u", ctxAttr.majorVersion());
		return std::make_pair(false, EGLConfig{});
	}
	if(Config::DEBUG_BUILD)
		printEGLConf(display, config);
	return std::make_pair(true, config);
}

void *GLContext::procAddress(const char *funcName)
{
	//logDMsg("getting proc address for:%s", funcName);
	return (void*)eglGetProcAddress(funcName);
}

EGLDisplay EGLContextBase::eglDisplay()
{
	if(display == EGL_NO_DISPLAY)
	{
		display = getDisplay();
		assert(display != EGL_NO_DISPLAY);
		if(!eglInitialize(display, nullptr, nullptr))
		{
			bug_exit("error initializing EGL");
			display = EGL_NO_DISPLAY;
			return display;
		}
		//logMsg("initialized EGL with display %ld", (long)display);
		if(Config::DEBUG_BUILD)
		{
			logMsg("version: %s (%s)", eglQueryString(display, EGL_VENDOR), eglQueryString(display, EGL_VERSION));
			logMsg("APIs: %s", eglQueryString(display, EGL_CLIENT_APIS));
			logMsg("extensions: %s", eglQueryString(display, EGL_EXTENSIONS));
			//printEGLConfs(display);
		}
	}
	return display;
}

std::error_code EGLContextBase::init(GLContextAttributes attr, GLBufferConfig config)
{
	if(eglDisplay() == EGL_NO_DISPLAY)
	{
		logErr("unable to get EGL display");
		return {ENOENT, std::system_category()};
	}
	deinit();
	logMsg("making context with version: %d.%d", attr.majorVersion(), attr.minorVersion());
	context = eglCreateContext(display, config.glConfig, EGL_NO_CONTEXT, &glContextAttrsToEGLAttrs(attr)[0]);
	if(context == EGL_NO_CONTEXT)
	{
		if(attr.debug())
		{
			logMsg("retrying without debug bit");
			attr.setDebug(false);
			context = eglCreateContext(display, config.glConfig, EGL_NO_CONTEXT, &glContextAttrsToEGLAttrs(attr)[0]);
		}
		if(context == EGL_NO_CONTEXT)
		{
			if(Config::DEBUG_BUILD)
				logErr("error creating context: 0x%X", (int)eglGetError());
			return {EINVAL, std::system_category()};
		}
	}
	// TODO: EGL 1.5 or higher supports surfaceless without any extension
	bool supportsSurfaceless = strstr(eglQueryString(display, EGL_EXTENSIONS), "EGL_KHR_surfaceless_context");
	if(!supportsSurfaceless)
	{
		logMsg("surfaceless context not supported");
		if(!hasDummyPbuffConfig)
		{
			dummyPbuffConfig = config.glConfig;
			hasDummyPbuffConfig = true;
		}
		else
		{
			// all contexts must use same config if surfaceless isn't supported
			assert(dummyPbuffConfig == config.glConfig);
		}
	}
	return {};
}

void EGLContextBase::setCurrentContext(EGLContext context, Window *win)
{
	assert(display != EGL_NO_DISPLAY);
	if(context == EGL_NO_CONTEXT)
	{
		logMsg("making no context current");
		assert(!win);
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
	else if(win)
	{
		assert(context != EGL_NO_CONTEXT);
		auto surface = win->eglSurface();
		logMsg("setting surface 0x%lX current", (long)surface);
		if(eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
		{
			bug_exit("error setting surface current");
		}
	}
	else
	{
		assert(context != EGL_NO_CONTEXT);
		if(hasDummyPbuffConfig)
		{
			logMsg("setting dummy pbuffer surface current");
			auto dummyPbuff = makeDummyPbuffer(display, dummyPbuffConfig);
			assert(dummyPbuff != EGL_NO_SURFACE);
			if(eglMakeCurrent(display, dummyPbuff, dummyPbuff, context) == EGL_FALSE)
			{
				bug_exit("error setting dummy pbuffer current");
			}
			eglDestroySurface(display, dummyPbuff);
		}
		else
		{
			logMsg("setting no surface current");
			if(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_FALSE)
			{
				bug_exit("error setting no surface current");
			}
		}
	}
}

void GLContext::setDrawable(Window *win)
{
	setDrawable(win, current());
}

void GLContext::setDrawable(Window *win, GLContext cachedCurrentContext)
{
	setCurrentContext(cachedCurrentContext.context, win);
}

GLContext GLContext::current()
{
	GLContext c;
	c.context = eglGetCurrentContext();
	return c;
}

void EGLContextBase::swapBuffers(Window &win)
{
	assert(display != EGL_NO_DISPLAY);
	auto surface = win.eglSurface();
	assert(surface != EGL_NO_SURFACE);
	if(eglSwapBuffers(display, surface) == EGL_FALSE)
	{
		bug_exit("error 0x%X swapping buffers for window: %p", eglGetError(), &win);
	}
}

GLContext::operator bool() const
{
	return context != EGL_NO_CONTEXT;
}

bool GLContext::operator ==(GLContext const &rhs) const
{
	return context == rhs.context;
}

void EGLContextBase::deinit()
{
	if(context != EGL_NO_CONTEXT)
	{
		logMsg("destroying EGL context");
		eglDestroyContext(display, context);
		context = EGL_NO_CONTEXT;
	}
}

}
