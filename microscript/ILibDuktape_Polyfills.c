/*
Copyright 2006 - 2018 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#include <Windows.h>
#include <WinBase.h>
#endif

#include "duktape.h"
#include "ILibDuktape_Helpers.h"
#include "ILibDuktapeModSearch.h"
#include "ILibDuktape_DuplexStream.h"
#include "ILibDuktape_EventEmitter.h"
#include "ILibDuktape_Debugger.h"
#include "../microstack/ILibParsers.h"
#include "../microstack/ILibCrypto.h"
#include "../microstack/ILibRemoteLogging.h"


#define ILibDuktape_Timer_Ptrs					"\xFF_DuktapeTimer_PTRS"
#define ILibDuktape_Queue_Ptr					"\xFF_Queue"
#define ILibDuktape_Stream_Buffer				"\xFF_BUFFER"
#define ILibDuktape_Stream_ReadablePtr			"\xFF_ReadablePtr"
#define ILibDuktape_Stream_WritablePtr			"\xFF_WritablePtr"
#define ILibDuktape_Console_Destination			"\xFF_Console_Destination"
#define ILibDuktape_Console_LOG_Destination		"\xFF_Console_Destination"
#define ILibDuktape_Console_WARN_Destination	"\xFF_Console_WARN_Destination"
#define ILibDuktape_Console_ERROR_Destination	"\xFF_Console_ERROR_Destination"
#define ILibDuktape_Console_INFO_Level			"\xFF_Console_INFO_Level"
#define ILibDuktape_Console_SessionID			"\xFF_Console_SessionID"

#define ILibDuktape_DescriptorEvents_ChainLink	"\xFF_DescriptorEvents_ChainLink"
#define ILibDuktape_DescriptorEvents_Table		"\xFF_DescriptorEvents_Table"
#define ILibDuktape_DescriptorEvents_HTable		"\xFF_DescriptorEvents_HTable"
#define ILibDuktape_DescriptorEvents_CURRENT	"\xFF_DescriptorEvents_CURRENT"
#define ILibDuktape_DescriptorEvents_FD			"\xFF_DescriptorEvents_FD"
#define ILibDuktape_DescriptorEvents_Options	"\xFF_DescriptorEvents_Options"
#define ILibDuktape_DescriptorEvents_WaitHandle "\xFF_DescriptorEvents_WindowsWaitHandle"
#define ILibDuktape_ChainViewer_PromiseList		"\xFF_ChainViewer_PromiseList"
#define CP_ISO8859_1							28591

#define ILibDuktape_AltRequireTable				"\xFF_AltRequireTable"
#define ILibDuktape_AddCompressedModule(ctx, name, b64str) duk_push_global_object(ctx);duk_get_prop_string(ctx, -1, "addCompressedModule");duk_swap_top(ctx, -2);duk_push_string(ctx, name);duk_push_global_object(ctx);duk_get_prop_string(ctx, -1, "Buffer"); duk_remove(ctx, -2);duk_get_prop_string(ctx, -1, "from");duk_swap_top(ctx, -2);duk_push_string(ctx, b64str);duk_push_string(ctx, "base64");duk_pcall_method(ctx, 2);duk_pcall_method(ctx, 2);duk_pop(ctx);
#define ILibDuktape_AddCompressedModuleEx(ctx, name, b64str, stamp) duk_push_global_object(ctx);duk_get_prop_string(ctx, -1, "addCompressedModule");duk_swap_top(ctx, -2);duk_push_string(ctx, name);duk_push_global_object(ctx);duk_get_prop_string(ctx, -1, "Buffer"); duk_remove(ctx, -2);duk_get_prop_string(ctx, -1, "from");duk_swap_top(ctx, -2);duk_push_string(ctx, b64str);duk_push_string(ctx, "base64");duk_pcall_method(ctx, 2);duk_push_string(ctx,stamp);duk_pcall_method(ctx, 3);duk_pop(ctx);

extern void* _duk_get_first_object(void *ctx);
extern void* _duk_get_next_object(void *ctx, void *heapptr);


typedef enum ILibDuktape_Console_DestinationFlags
{
	ILibDuktape_Console_DestinationFlags_DISABLED		= 0,
	ILibDuktape_Console_DestinationFlags_StdOut			= 1,
	ILibDuktape_Console_DestinationFlags_ServerConsole	= 2,
	ILibDuktape_Console_DestinationFlags_WebLog			= 4,
	ILibDuktape_Console_DestinationFlags_LogFile		= 8
}ILibDuktape_Console_DestinationFlags;

#ifdef WIN32
typedef struct ILibDuktape_DescriptorEvents_WindowsWaitHandle
{
	HANDLE waitHandle;
	HANDLE eventThread;
	void *chain;
	duk_context *ctx;
	void *object;
}ILibDuktape_DescriptorEvents_WindowsWaitHandle;
#endif

int g_displayStreamPipeMessages = 0;
int g_displayFinalizerMessages = 0;
extern int GenerateSHA384FileHash(char *filePath, char *fileHash);

duk_ret_t ILibDuktape_Pollyfills_Buffer_slice(duk_context *ctx)
{
	int nargs = duk_get_top(ctx);
	char *buffer;
	char *out;
	duk_size_t bufferLen;
	int offset = 0;
	duk_push_this(ctx);

	buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);
	if (nargs >= 1)
	{
		offset = duk_require_int(ctx, 0);
		bufferLen -= offset;
	}
	if (nargs == 2)
	{
		bufferLen = (duk_size_t)duk_require_int(ctx, 1) - offset;
	}
	duk_push_fixed_buffer(ctx, bufferLen);
	out = Duktape_GetBuffer(ctx, -1, NULL);
	memcpy_s(out, bufferLen, buffer + offset, bufferLen);
	return 1;
}
duk_ret_t ILibDuktape_Polyfills_Buffer_randomFill(duk_context *ctx)
{
	int start, length;
	char *buffer;
	duk_size_t bufferLen;

	start = (int)(duk_get_top(ctx) == 0 ? 0 : duk_require_int(ctx, 0));
	length = (int)(duk_get_top(ctx) == 2 ? duk_require_int(ctx, 1) : -1);

	duk_push_this(ctx);
	buffer = (char*)Duktape_GetBuffer(ctx, -1, &bufferLen);
	if ((duk_size_t)length > bufferLen || length < 0)
	{
		length = (int)(bufferLen - start);
	}

	util_random(length, buffer + start);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Buffer_toString(duk_context *ctx)
{
	int nargs = duk_get_top(ctx);
	char *buffer, *tmpBuffer;
	duk_size_t bufferLen = 0;
	char *cType;

	duk_push_this(ctx);									// [buffer]
	buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);

	if (nargs == 0)
	{
		if (bufferLen == 0 || buffer == NULL)
		{
			duk_push_null(ctx);
		}
		else
		{
			// Just convert to a string
			duk_push_lstring(ctx, buffer, strnlen_s(buffer, bufferLen));			// [buffer][string]
		}
	}
	else
	{
		cType = (char*)duk_require_string(ctx, 0);
		if (strcmp(cType, "base64") == 0)
		{
			duk_push_fixed_buffer(ctx, ILibBase64EncodeLength(bufferLen));
			tmpBuffer = Duktape_GetBuffer(ctx, -1, NULL);
			ILibBase64Encode((unsigned char*)buffer, (int)bufferLen, (unsigned char**)&tmpBuffer);
			duk_push_string(ctx, tmpBuffer);
		}
		else if (strcmp(cType, "hex") == 0)
		{
			duk_push_fixed_buffer(ctx, 1 + (bufferLen * 2));
			tmpBuffer = Duktape_GetBuffer(ctx, -1, NULL);
			util_tohex(buffer, (int)bufferLen, tmpBuffer);
			duk_push_string(ctx, tmpBuffer);
		}
		else if (strcmp(cType, "hex:") == 0)
		{
			duk_push_fixed_buffer(ctx, 1 + (bufferLen * 3));
			tmpBuffer = Duktape_GetBuffer(ctx, -1, NULL);
			util_tohex2(buffer, (int)bufferLen, tmpBuffer);
			duk_push_string(ctx, tmpBuffer);
		}
#ifdef WIN32
		else if (strcmp(cType, "utf16") == 0)
		{
			int sz = (MultiByteToWideChar(CP_UTF8, 0, buffer, (int)bufferLen, NULL, 0) * 2);
			WCHAR* b = duk_push_fixed_buffer(ctx, sz);
			duk_push_buffer_object(ctx, -1, 0, sz, DUK_BUFOBJ_NODEJS_BUFFER);
			MultiByteToWideChar(CP_UTF8, 0, buffer, (int)bufferLen, b, sz / 2);
		}
#endif
		else
		{
			return(ILibDuktape_Error(ctx, "Unrecognized parameter"));
		}
	}
	return 1;
}
duk_ret_t ILibDuktape_Polyfills_Buffer_from(duk_context *ctx)
{
	int nargs = duk_get_top(ctx);
	char *str;
	duk_size_t strlength;
	char *encoding;
	char *buffer;
	size_t bufferLen;

	if (nargs == 1)
	{
		str = (char*)duk_get_lstring(ctx, 0, &strlength);
		buffer = duk_push_fixed_buffer(ctx, strlength);
		memcpy_s(buffer, strlength, str, strlength);
		duk_push_buffer_object(ctx, -1, 0, strlength, DUK_BUFOBJ_NODEJS_BUFFER);
		return(1);
	}
	else if(!(nargs == 2 && duk_is_string(ctx, 0) && duk_is_string(ctx, 1)))
	{
		return(ILibDuktape_Error(ctx, "usage not supported yet"));
	}

	str = (char*)duk_get_lstring(ctx, 0, &strlength);
	encoding = (char*)duk_require_string(ctx, 1);

	if (strcmp(encoding, "base64") == 0)
	{
		// Base64		
		buffer = duk_push_fixed_buffer(ctx, ILibBase64DecodeLength(strlength));
		bufferLen = ILibBase64Decode((unsigned char*)str, (int)strlength, (unsigned char**)&buffer);
		duk_push_buffer_object(ctx, -1, 0, bufferLen, DUK_BUFOBJ_NODEJS_BUFFER);
	}
	else if (strcmp(encoding, "hex") == 0)
	{		
		if (ILibString_StartsWith(str, (int)strlength, "0x", 2) != 0)
		{
			str += 2;
			strlength -= 2;
		}
		buffer = duk_push_fixed_buffer(ctx, strlength / 2);
		bufferLen = util_hexToBuf(str, (int)strlength, buffer);
		duk_push_buffer_object(ctx, -1, 0, bufferLen, DUK_BUFOBJ_NODEJS_BUFFER);
	}
	else if (strcmp(encoding, "utf8") == 0)
	{
		str = (char*)duk_get_lstring(ctx, 0, &strlength);
		buffer = duk_push_fixed_buffer(ctx, strlength);
		memcpy_s(buffer, strlength, str, strlength);
		duk_push_buffer_object(ctx, -1, 0, strlength, DUK_BUFOBJ_NODEJS_BUFFER);
		return(1);
	}
	else if (strcmp(encoding, "binary") == 0)
	{
		str = (char*)duk_get_lstring(ctx, 0, &strlength);

#ifdef WIN32
		int r = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)str, (int)strlength, NULL, 0);
		buffer = duk_push_fixed_buffer(ctx, 2 + (2 * r));
		strlength = (duk_size_t)MultiByteToWideChar(CP_UTF8, 0, (LPCCH)str, (int)strlength, (LPWSTR)buffer, r + 1);
		r = (int)WideCharToMultiByte(CP_ISO8859_1, 0, (LPCWCH)buffer, (int)strlength, NULL, 0, NULL, FALSE);
		duk_push_fixed_buffer(ctx, r);
		WideCharToMultiByte(CP_ISO8859_1, 0, (LPCWCH)buffer, (int)strlength, (LPSTR)Duktape_GetBuffer(ctx, -1, NULL), r, NULL, FALSE);
		duk_push_buffer_object(ctx, -1, 0, r, DUK_BUFOBJ_NODEJS_BUFFER);
#else
		duk_eval_string(ctx, "Buffer.fromBinary");	// [func]
		duk_dup(ctx, 0);
		duk_call(ctx, 1);
#endif
	}
	else
	{
		return(ILibDuktape_Error(ctx, "unsupported encoding"));
	}
	return 1;
}
duk_ret_t ILibDuktape_Polyfills_Buffer_readInt32BE(duk_context *ctx)
{
	int offset = duk_require_int(ctx, 0);
	char *buffer;
	duk_size_t bufferLen;

	duk_push_this(ctx);
	buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);

	duk_push_int(ctx, ntohl(((int*)(buffer + offset))[0]));
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Buffer_alloc(duk_context *ctx)
{
	int sz = duk_require_int(ctx, 0);
	int fill = 0;

	if (duk_is_number(ctx, 1)) { fill = duk_require_int(ctx, 1); }

	duk_push_fixed_buffer(ctx, sz);
	char *buffer = Duktape_GetBuffer(ctx, -1, NULL);
	memset(buffer, fill, sz);
	duk_push_buffer_object(ctx, -1, 0, sz, DUK_BUFOBJ_NODEJS_BUFFER);
	return(1);
}

void ILibDuktape_Polyfills_Buffer(duk_context *ctx)
{
	char extras[] =
		"Object.defineProperty(Buffer.prototype, \"swap32\",\
	{\
		value: function swap32()\
		{\
			var a = this.readUInt16BE(0);\
			var b = this.readUInt16BE(2);\
			this.writeUInt16LE(a, 2);\
			this.writeUInt16LE(b, 0);\
			return(this);\
		}\
	});";
	duk_eval_string(ctx, extras); duk_pop(ctx);

#ifdef _POSIX
	char fromBinary[] =
		"Object.defineProperty(Buffer, \"fromBinary\",\
		{\
			get: function()\
			{\
				return((function fromBinary(str)\
						{\
							var child = require('child_process').execFile('/usr/bin/iconv', ['iconv', '-c','-f', 'UTF-8', '-t', 'CP819']);\
							child.stdout.buf = Buffer.alloc(0);\
							child.stdout.on('data', function(c) { this.buf = Buffer.concat([this.buf, c]); });\
							child.stdin.write(str);\
							child.stderr.on('data', function(c) { });\
							child.stdin.end();\
							child.waitExit();\
							return(child.stdout.buf);\
						}));\
			}\
		});";
	duk_eval_string_noresult(ctx, fromBinary);

#endif

	// Polyfill Buffer.from()
	duk_get_prop_string(ctx, -1, "Buffer");											// [g][Buffer]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Buffer_from, DUK_VARARGS);		// [g][Buffer][func]
	duk_put_prop_string(ctx, -2, "from");											// [g][Buffer]
	duk_pop(ctx);																	// [g]

	// Polyfill Buffer.alloc() for Node Buffers)
	duk_get_prop_string(ctx, -1, "Buffer");											// [g][Buffer]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Buffer_alloc, DUK_VARARGS);		// [g][Buffer][func]
	duk_put_prop_string(ctx, -2, "alloc");											// [g][Buffer]
	duk_pop(ctx);																	// [g]


	// Polyfill Buffer.toString() for Node Buffers
	duk_get_prop_string(ctx, -1, "Buffer");											// [g][Buffer]
	duk_get_prop_string(ctx, -1, "prototype");										// [g][Buffer][prototype]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Buffer_toString, DUK_VARARGS);	// [g][Buffer][prototype][func]
	duk_put_prop_string(ctx, -2, "toString");										// [g][Buffer][prototype]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Buffer_randomFill, DUK_VARARGS);	// [g][Buffer][prototype][func]
	duk_put_prop_string(ctx, -2, "randomFill");										// [g][Buffer][prototype]
	duk_pop_2(ctx);																	// [g]
}
duk_ret_t ILibDuktape_Polyfills_String_startsWith(duk_context *ctx)
{
	duk_size_t tokenLen;
	char *token = Duktape_GetBuffer(ctx, 0, &tokenLen);
	char *buffer;
	duk_size_t bufferLen;

	duk_push_this(ctx);
	buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);

	if (ILibString_StartsWith(buffer, (int)bufferLen, token, (int)tokenLen) != 0)
	{
		duk_push_true(ctx);
	}
	else
	{
		duk_push_false(ctx);
	}

	return 1;
}
duk_ret_t ILibDuktape_Polyfills_String_endsWith(duk_context *ctx)
{
	duk_size_t tokenLen;
	char *token = Duktape_GetBuffer(ctx, 0, &tokenLen);
	char *buffer;
	duk_size_t bufferLen;

	duk_push_this(ctx);
	buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);
	
	if (ILibString_EndsWith(buffer, (int)bufferLen, token, (int)tokenLen) != 0)
	{
		duk_push_true(ctx);
	}
	else
	{
		duk_push_false(ctx);
	}

	return 1;
}
duk_ret_t ILibDuktape_Polyfills_String_padStart(duk_context *ctx)
{
	int totalLen = (int)duk_require_int(ctx, 0);

	duk_size_t padcharLen;
	duk_size_t bufferLen;

	char *padchars;
	if (duk_get_top(ctx) > 1)
	{
		padchars = (char*)duk_get_lstring(ctx, 1, &padcharLen);
	}
	else
	{
		padchars = " ";
		padcharLen = 1;
	}

	duk_push_this(ctx);
	char *buffer = Duktape_GetBuffer(ctx, -1, &bufferLen);

	if ((int)bufferLen > totalLen)
	{
		duk_push_lstring(ctx, buffer, bufferLen);
		return(1);
	}
	else
	{
		duk_size_t needs = totalLen - bufferLen;

		duk_push_array(ctx);											// [array]
		while(needs > 0)
		{
			if (needs > padcharLen)
			{
				duk_push_string(ctx, padchars);							// [array][pad]
				duk_put_prop_index(ctx, -2, (duk_uarridx_t)duk_get_length(ctx, -2));	// [array]
				needs -= padcharLen;
			}
			else
			{
				duk_push_lstring(ctx, padchars, needs);					// [array][pad]
				duk_put_prop_index(ctx, -2, (duk_uarridx_t)duk_get_length(ctx, -2));	// [array]
				needs = 0;
			}
		}
		duk_push_lstring(ctx, buffer, bufferLen);						// [array][pad]
		duk_put_prop_index(ctx, -2, (duk_uarridx_t)duk_get_length(ctx, -2));			// [array]
		duk_get_prop_string(ctx, -1, "join");							// [array][join]
		duk_swap_top(ctx, -2);											// [join][this]
		duk_push_string(ctx, "");										// [join][this]['']
		duk_call_method(ctx, 1);										// [result]
		return(1);
	}
}
duk_ret_t ILibDuktape_Polyfills_Array_includes(duk_context *ctx)
{
	duk_push_this(ctx);										// [array]
	uint32_t count = (uint32_t)duk_get_length(ctx, -1);
	uint32_t i;
	for (i = 0; i < count; ++i)
	{
		duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);		// [array][val1]
		duk_dup(ctx, 0);									// [array][val1][val2]
		if (duk_equals(ctx, -2, -1))
		{
			duk_push_true(ctx);
			return(1);
		}
		else
		{
			duk_pop_2(ctx);									// [array]
		}
	}
	duk_push_false(ctx);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Array_partialIncludes(duk_context *ctx)
{
	duk_size_t inLen;
	char *inStr = (char*)duk_get_lstring(ctx, 0, &inLen);
	duk_push_this(ctx);										// [array]
	uint32_t count = (uint32_t)duk_get_length(ctx, -1);
	uint32_t i;
	duk_size_t tmpLen;
	char *tmp;
	for (i = 0; i < count; ++i)
	{
		tmp = Duktape_GetStringPropertyIndexValueEx(ctx, -1, i, "", &tmpLen);
		if (inLen > 0 && inLen <= tmpLen && strncmp(inStr, tmp, inLen) == 0)
		{
			duk_push_int(ctx, i);
			return(1);
		}
	}
	duk_push_int(ctx, -1);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Array_find(duk_context *ctx)
{
	duk_push_this(ctx);								// [array]
	duk_prepare_method_call(ctx, -1, "findIndex");	// [array][findIndex][this]
	duk_dup(ctx, 0);								// [array][findIndex][this][func]
	duk_call_method(ctx, 1);						// [array][result]
	if (duk_get_int(ctx, -1) == -1) { duk_push_undefined(ctx); return(1); }
	duk_get_prop(ctx, -2);							// [element]
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Array_findIndex(duk_context *ctx)
{
	duk_idx_t nargs = duk_get_top(ctx);
	duk_push_this(ctx);								// [array]

	duk_size_t sz = duk_get_length(ctx, -1);
	duk_uarridx_t i;

	for (i = 0; i < sz; ++i)
	{
		duk_dup(ctx, 0);							// [array][func]
		if (nargs > 1 && duk_is_function(ctx, 1))
		{
			duk_dup(ctx, 1);						// [array][func][this]
		}
		else
		{
			duk_push_this(ctx);						// [array][func][this]
		}
		duk_get_prop_index(ctx, -3, i);				// [array][func][this][element]
		duk_push_uint(ctx, i);						// [array][func][this][element][index]
		duk_push_this(ctx);							// [array][func][this][element][index][array]
		duk_call_method(ctx, 3);					// [array][ret]
		if (!duk_is_undefined(ctx, -1) && duk_is_boolean(ctx, -1) && duk_to_boolean(ctx, -1) != 0)
		{
			duk_push_uint(ctx, i);
			return(1);
		}
		duk_pop(ctx);								// [array]
	}
	duk_push_int(ctx, -1);
	return(1);
}
void ILibDuktape_Polyfills_Array(duk_context *ctx)
{
	duk_get_prop_string(ctx, -1, "Array");											// [Array]
	duk_get_prop_string(ctx, -1, "prototype");										// [Array][proto]

	// Polyfill 'Array.includes'
	ILibDuktape_CreateProperty_InstanceMethod_SetEnumerable(ctx, "includes", ILibDuktape_Polyfills_Array_includes, 1, 0);

	// Polyfill 'Array.partialIncludes'
	ILibDuktape_CreateProperty_InstanceMethod_SetEnumerable(ctx, "partialIncludes", ILibDuktape_Polyfills_Array_partialIncludes, 1, 0);

	// Polyfill 'Array.find'
	ILibDuktape_CreateProperty_InstanceMethod_SetEnumerable(ctx, "find", ILibDuktape_Polyfills_Array_find, 1, 0);

	// Polyfill 'Array.findIndex'
	ILibDuktape_CreateProperty_InstanceMethod_SetEnumerable(ctx, "findIndex", ILibDuktape_Polyfills_Array_findIndex, DUK_VARARGS, 0);
	duk_pop_2(ctx);																	// ...
}
void ILibDuktape_Polyfills_String(duk_context *ctx)
{
	// Polyfill 'String.startsWith'
	duk_get_prop_string(ctx, -1, "String");											// [string]
	duk_get_prop_string(ctx, -1, "prototype");										// [string][proto]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_String_startsWith, DUK_VARARGS);	// [string][proto][func]
	duk_put_prop_string(ctx, -2, "startsWith");										// [string][proto]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_String_endsWith, DUK_VARARGS);	// [string][proto][func]
	duk_put_prop_string(ctx, -2, "endsWith");										// [string][proto]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_String_padStart, DUK_VARARGS);	// [string][proto][func]
	duk_put_prop_string(ctx, -2, "padStart");
	duk_pop_2(ctx);
}
duk_ret_t ILibDuktape_Polyfills_Console_log(duk_context *ctx)
{
	int numargs = duk_get_top(ctx);
	int i, x;
	duk_size_t strLen;
	char *str;
	char *PREFIX = NULL;
	char *DESTINATION = NULL;
	duk_push_current_function(ctx);
	ILibDuktape_LogTypes logType = (ILibDuktape_LogTypes)Duktape_GetIntPropertyValue(ctx, -1, "logType", ILibDuktape_LogType_Normal);
	switch (logType)
	{
		case ILibDuktape_LogType_Warn:
			PREFIX = (char*)"WARNING: "; // LENGTH MUST BE <= 9
			DESTINATION = ILibDuktape_Console_WARN_Destination;
			break;
		case ILibDuktape_LogType_Error:
			PREFIX = (char*)"ERROR: "; // LENGTH MUST BE <= 9
			DESTINATION = ILibDuktape_Console_ERROR_Destination;
			break;
		case ILibDuktape_LogType_Info1:
		case ILibDuktape_LogType_Info2:
		case ILibDuktape_LogType_Info3:
			duk_push_this(ctx);
			i = Duktape_GetIntPropertyValue(ctx, -1, ILibDuktape_Console_INFO_Level, 0);
			duk_pop(ctx);
			PREFIX = NULL;
			if (i >= (((int)logType + 1) - (int)ILibDuktape_LogType_Info1))
			{
				DESTINATION = ILibDuktape_Console_LOG_Destination;
			}
			else
			{
				return(0);
			}
			break;
		default:
			PREFIX = NULL;
			DESTINATION = ILibDuktape_Console_LOG_Destination;
			break;
	}
	duk_pop(ctx);

	// Calculate total length of string
	strLen = 0;
	strLen += snprintf(NULL, 0, "%s", PREFIX != NULL ? PREFIX : "");
	for (i = 0; i < numargs; ++i)
	{
		if (duk_is_string(ctx, i))
		{
			strLen += snprintf(NULL, 0, "%s%s", (i == 0 ? "" : ", "), duk_require_string(ctx, i));
		}
		else
		{
			duk_dup(ctx, i);
			if (strcmp("[object Object]", duk_to_string(ctx, -1)) == 0)
			{
				duk_pop(ctx);
				duk_dup(ctx, i);
				strLen += snprintf(NULL, 0, "%s", (i == 0 ? "{" : ", {"));
				duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
				int propNum = 0;
				while (duk_next(ctx, -1, 1))
				{
					strLen += snprintf(NULL, 0, "%s%s: %s", ((propNum++ == 0) ? " " : ", "), (char*)duk_to_string(ctx, -2), (char*)duk_to_string(ctx, -1));
					duk_pop_2(ctx);
				}
				duk_pop(ctx);
				strLen += snprintf(NULL, 0, " }");
			}
			else
			{
				strLen += snprintf(NULL, 0, "%s%s", (i == 0 ? "" : ", "), duk_to_string(ctx, -1));
			}
		}
	}
	strLen += snprintf(NULL, 0, "\n");
	strLen += 1;

	str = Duktape_PushBuffer(ctx, strLen);
	x = 0;
	for (i = 0; i < numargs; ++i)
	{
		if (duk_is_string(ctx, i))
		{
			x += sprintf_s(str + x, strLen - x, "%s%s", (i == 0 ? "" : ", "), duk_require_string(ctx, i));
		}
		else
		{
			duk_dup(ctx, i);
			if (strcmp("[object Object]", duk_to_string(ctx, -1)) == 0)
			{
				duk_pop(ctx);
				duk_dup(ctx, i);
				x += sprintf_s(str+x, strLen - x, "%s", (i == 0 ? "{" : ", {"));
				duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
				int propNum = 0;
				while (duk_next(ctx, -1, 1))
				{
					x += sprintf_s(str + x, strLen - x, "%s%s: %s", ((propNum++ == 0) ? " " : ", "), (char*)duk_to_string(ctx, -2), (char*)duk_to_string(ctx, -1));
					duk_pop_2(ctx);
				}
				duk_pop(ctx);
				x += sprintf_s(str + x, strLen - x, " }");
			}
			else
			{
				x += sprintf_s(str + x, strLen - x, "%s%s", (i == 0 ? "" : ", "), duk_to_string(ctx, -1));
			}
		}
	}
	x += sprintf_s(str + x, strLen - x, "\n");

	duk_push_this(ctx);		// [console]
	int dest = Duktape_GetIntPropertyValue(ctx, -1, DESTINATION, ILibDuktape_Console_DestinationFlags_StdOut);

	if ((dest & ILibDuktape_Console_DestinationFlags_StdOut) == ILibDuktape_Console_DestinationFlags_StdOut)
	{
#ifdef WIN32
		DWORD writeLen;
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), (void*)str, x, &writeLen, NULL);
#else
		ignore_result(write(STDOUT_FILENO, str, x));
#endif
	}
	if ((dest & ILibDuktape_Console_DestinationFlags_WebLog) == ILibDuktape_Console_DestinationFlags_WebLog)
	{
		ILibRemoteLogging_printf(ILibChainGetLogger(Duktape_GetChain(ctx)), ILibRemoteLogging_Modules_Microstack_Generic, ILibRemoteLogging_Flags_VerbosityLevel_1, "%s", str);
	}
	if ((dest & ILibDuktape_Console_DestinationFlags_ServerConsole) == ILibDuktape_Console_DestinationFlags_ServerConsole)
	{
		if (duk_peval_string(ctx, "require('MeshAgent');") == 0)
		{
			duk_get_prop_string(ctx, -1, "SendCommand");	// [console][agent][SendCommand]
			duk_swap_top(ctx, -2);							// [console][SendCommand][this]
			duk_push_object(ctx);							// [console][SendCommand][this][options]
			duk_push_string(ctx, "msg"); duk_put_prop_string(ctx, -2, "action");
			duk_push_string(ctx, "console"); duk_put_prop_string(ctx, -2, "type");
			duk_push_string(ctx, str); duk_put_prop_string(ctx, -2, "value");
			if (duk_has_prop_string(ctx, -4, ILibDuktape_Console_SessionID))
			{
				duk_get_prop_string(ctx, -4, ILibDuktape_Console_SessionID);
				duk_put_prop_string(ctx, -2, "sessionid");
			}
			duk_call_method(ctx, 1);
		}
	}
	if ((dest & ILibDuktape_Console_DestinationFlags_LogFile) == ILibDuktape_Console_DestinationFlags_LogFile)
	{
		duk_size_t pathLen;
		char *path;
		char *tmp = (char*)ILibMemory_SmartAllocate(x + 32);
		int tmpx = ILibGetLocalTime(tmp + 1, (int)ILibMemory_Size(tmp) - 1) + 1;
		tmp[0] = '[';
		tmp[tmpx] = ']';
		tmp[tmpx + 1] = ':';
		tmp[tmpx + 2] = ' ';
		memcpy_s(tmp + tmpx + 3, ILibMemory_Size(tmp) - tmpx - 3, str, x);
		duk_eval_string(ctx, "require('fs');");
		duk_get_prop_string(ctx, -1, "writeFileSync");						// [fs][writeFileSync]
		duk_swap_top(ctx, -2);												// [writeFileSync][this]
		duk_push_heapptr(ctx, ILibDuktape_GetProcessObject(ctx));			// [writeFileSync][this][process]
		duk_get_prop_string(ctx, -1, "execPath");							// [writeFileSync][this][process][execPath]
		path = (char*)duk_get_lstring(ctx, -1, &pathLen);
		if (path != NULL)
		{
			if (ILibString_EndsWithEx(path, (int)pathLen, ".exe", 4, 0))
			{
				duk_get_prop_string(ctx, -1, "substring");						// [writeFileSync][this][process][execPath][substring]
				duk_swap_top(ctx, -2);											// [writeFileSync][this][process][substring][this]
				duk_push_int(ctx, 0);											// [writeFileSync][this][process][substring][this][0]
				duk_push_int(ctx, (int)(pathLen - 4));							// [writeFileSync][this][process][substring][this][0][len]
				duk_call_method(ctx, 2);										// [writeFileSync][this][process][path]
			}
			duk_get_prop_string(ctx, -1, "concat");								// [writeFileSync][this][process][path][concat]
			duk_swap_top(ctx, -2);												// [writeFileSync][this][process][concat][this]
			duk_push_string(ctx, ".jlog");										// [writeFileSync][this][process][concat][this][.jlog]
			duk_call_method(ctx, 1);											// [writeFileSync][this][process][logPath]
			duk_remove(ctx, -2);												// [writeFileSync][this][logPath]
			duk_push_string(ctx, tmp);											// [writeFileSync][this][logPath][log]
			duk_push_object(ctx);												// [writeFileSync][this][logPath][log][options]
			duk_push_string(ctx, "a"); duk_put_prop_string(ctx, -2, "flags");
			duk_pcall_method(ctx, 3);
		}
		ILibMemory_Free(tmp);
	}
	return 0;
}
duk_ret_t ILibDuktape_Polyfills_Console_enableWebLog(duk_context *ctx)
{
#ifdef _REMOTELOGGING
	void *chain = Duktape_GetChain(ctx);
	int port = duk_require_int(ctx, 0);
	duk_size_t pLen;
	if (duk_peval_string(ctx, "process.argv0") != 0) { return(ILibDuktape_Error(ctx, "console.enableWebLog(): Couldn't fetch argv0")); }
	char *p = (char*)duk_get_lstring(ctx, -1, &pLen);
	if (ILibString_EndsWith(p, (int)pLen, ".js", 3) != 0)
	{
		memcpy_s(ILibScratchPad2, sizeof(ILibScratchPad2), p, pLen - 3);
		sprintf_s(ILibScratchPad2 + (pLen - 3), sizeof(ILibScratchPad2) - 3, ".wlg");
	}
	else if (ILibString_EndsWith(p, (int)pLen, ".exe", 3) != 0)
	{
		memcpy_s(ILibScratchPad2, sizeof(ILibScratchPad2), p, pLen - 4);
		sprintf_s(ILibScratchPad2 + (pLen - 3), sizeof(ILibScratchPad2) - 4, ".wlg");
	}
	else
	{
		sprintf_s(ILibScratchPad2, sizeof(ILibScratchPad2), "%s.wlg", p);
	}
	ILibStartDefaultLoggerEx(chain, (unsigned short)port, ILibScratchPad2);
#endif
	return (0);
}
duk_ret_t ILibDuktape_Polyfills_Console_displayStreamPipe_getter(duk_context *ctx)
{
	duk_push_int(ctx, g_displayStreamPipeMessages);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Console_displayStreamPipe_setter(duk_context *ctx)
{
	g_displayStreamPipeMessages = duk_require_int(ctx, 0);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_displayFinalizer_getter(duk_context *ctx)
{
	duk_push_int(ctx, g_displayFinalizerMessages);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Console_displayFinalizer_setter(duk_context *ctx)
{
	g_displayFinalizerMessages = duk_require_int(ctx, 0);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_logRefCount(duk_context *ctx)
{
	duk_push_global_object(ctx); duk_get_prop_string(ctx, -1, "console");	// [g][console]
	duk_get_prop_string(ctx, -1, "log");									// [g][console][log]
	duk_swap_top(ctx, -2);													// [g][log][this]
	duk_push_sprintf(ctx, "Reference Count => %s[%p]:%d\n", Duktape_GetStringPropertyValue(ctx, 0, ILibDuktape_OBJID, "UNKNOWN"), duk_require_heapptr(ctx, 0), ILibDuktape_GetReferenceCount(ctx, 0) - 1);
	duk_call_method(ctx, 1);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_setDestination(duk_context *ctx)
{
	int nargs = duk_get_top(ctx);
	int dest = duk_require_int(ctx, 0);

	duk_push_this(ctx);						// console
	if ((dest & ILibDuktape_Console_DestinationFlags_ServerConsole) == ILibDuktape_Console_DestinationFlags_ServerConsole)
	{
		// Mesh Server Console
		if (duk_peval_string(ctx, "require('MeshAgent');") != 0) { return(ILibDuktape_Error(ctx, "Unable to set destination to Mesh Console ")); }
		duk_pop(ctx);
		if (nargs > 1)
		{
			duk_dup(ctx, 1);
			duk_put_prop_string(ctx, -2, ILibDuktape_Console_SessionID);
		}
		else
		{
			duk_del_prop_string(ctx, -1, ILibDuktape_Console_SessionID);
		}
	}
	duk_dup(ctx, 0);
	duk_put_prop_string(ctx, -2, ILibDuktape_Console_Destination);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_setInfoLevel(duk_context *ctx)
{
	int val = duk_require_int(ctx, 0);
	if (val < 0) { return(ILibDuktape_Error(ctx, "Invalid Info Level: %d", val)); }

	duk_push_this(ctx);
	duk_push_int(ctx, val);
	duk_put_prop_string(ctx, -2, ILibDuktape_Console_INFO_Level);

	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_setInfoMask(duk_context *ctx)
{
	ILIBLOGMESSAGEX2_SetMask(duk_require_uint(ctx, 0));
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_Console_rawLog(duk_context *ctx)
{
	char *val = (char*)duk_require_string(ctx, 0);
	ILIBLOGMESSAGEX("%s", val);
	return(0);
}
void ILibDuktape_Polyfills_Console(duk_context *ctx)
{
	// Polyfill console.log()
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

	if (duk_has_prop_string(ctx, -1, "console"))
	{
		duk_get_prop_string(ctx, -1, "console");									// [g][console]
	}
	else
	{
		duk_push_object(ctx);														// [g][console]
		duk_dup(ctx, -1);															// [g][console][console]
		duk_put_prop_string(ctx, -3, "console");									// [g][console]
	}

	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Normal, "log", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Warn, "warn", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Error, "error", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Info1, "info1", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Info2, "info2", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "logType", (int)ILibDuktape_LogType_Info3, "info3", ILibDuktape_Polyfills_Console_log, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "rawLog", ILibDuktape_Polyfills_Console_rawLog, 1);

	ILibDuktape_CreateInstanceMethod(ctx, "enableWebLog", ILibDuktape_Polyfills_Console_enableWebLog, 1);
	ILibDuktape_CreateEventWithGetterAndSetterEx(ctx, "displayStreamPipeMessages", ILibDuktape_Polyfills_Console_displayStreamPipe_getter, ILibDuktape_Polyfills_Console_displayStreamPipe_setter);
	ILibDuktape_CreateEventWithGetterAndSetterEx(ctx, "displayFinalizerMessages", ILibDuktape_Polyfills_Console_displayFinalizer_getter, ILibDuktape_Polyfills_Console_displayFinalizer_setter);
	ILibDuktape_CreateInstanceMethod(ctx, "logReferenceCount", ILibDuktape_Polyfills_Console_logRefCount, 1);
	
	ILibDuktape_CreateInstanceMethod(ctx, "setDestination", ILibDuktape_Polyfills_Console_setDestination, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "setInfoLevel", ILibDuktape_Polyfills_Console_setInfoLevel, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "setInfoMask", ILibDuktape_Polyfills_Console_setInfoMask, 1);

	duk_push_object(ctx);
	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_DISABLED); duk_put_prop_string(ctx, -2, "DISABLED");
	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_StdOut); duk_put_prop_string(ctx, -2, "STDOUT");
	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_ServerConsole); duk_put_prop_string(ctx, -2, "SERVERCONSOLE");
	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_WebLog); duk_put_prop_string(ctx, -2, "WEBLOG");
	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_LogFile); duk_put_prop_string(ctx, -2, "LOGFILE");
	ILibDuktape_CreateReadonlyProperty(ctx, "Destinations");

	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_StdOut | ILibDuktape_Console_DestinationFlags_LogFile);
	duk_put_prop_string(ctx, -2, ILibDuktape_Console_ERROR_Destination);

	duk_push_int(ctx, ILibDuktape_Console_DestinationFlags_StdOut | ILibDuktape_Console_DestinationFlags_LogFile);
	duk_put_prop_string(ctx, -2, ILibDuktape_Console_WARN_Destination);

	duk_push_int(ctx, 0); duk_put_prop_string(ctx, -2, ILibDuktape_Console_INFO_Level);

	duk_pop(ctx);																	// [g]
}
duk_ret_t ILibDuktape_ntohl(duk_context *ctx)
{
	duk_size_t bufferLen;
	char *buffer = Duktape_GetBuffer(ctx, 0, &bufferLen);
	int offset = duk_require_int(ctx, 1);

	if ((int)bufferLen < (4 + offset)) { return(ILibDuktape_Error(ctx, "buffer too small")); }
	duk_push_int(ctx, ntohl(((unsigned int*)(buffer + offset))[0]));
	return 1;
}
duk_ret_t ILibDuktape_ntohs(duk_context *ctx)
{
	duk_size_t bufferLen;
	char *buffer = Duktape_GetBuffer(ctx, 0, &bufferLen);
	int offset = duk_require_int(ctx, 1);

	if ((int)bufferLen < 2 + offset) { return(ILibDuktape_Error(ctx, "buffer too small")); }
	duk_push_int(ctx, ntohs(((unsigned short*)(buffer + offset))[0]));
	return 1;
}
duk_ret_t ILibDuktape_htonl(duk_context *ctx)
{
	duk_size_t bufferLen;
	char *buffer = Duktape_GetBuffer(ctx, 0, &bufferLen);
	int offset = duk_require_int(ctx, 1);
	unsigned int val = (unsigned int)duk_require_int(ctx, 2);

	if ((int)bufferLen < (4 + offset)) { return(ILibDuktape_Error(ctx, "buffer too small")); }
	((unsigned int*)(buffer + offset))[0] = htonl(val);
	return 0;
}
duk_ret_t ILibDuktape_htons(duk_context *ctx)
{
	duk_size_t bufferLen;
	char *buffer = Duktape_GetBuffer(ctx, 0, &bufferLen);
	int offset = duk_require_int(ctx, 1);
	unsigned int val = (unsigned int)duk_require_int(ctx, 2);

	if ((int)bufferLen < (2 + offset)) { return(ILibDuktape_Error(ctx, "buffer too small")); }
	((unsigned short*)(buffer + offset))[0] = htons(val);
	return 0;
}
void ILibDuktape_Polyfills_byte_ordering(duk_context *ctx)
{
	ILibDuktape_CreateInstanceMethod(ctx, "ntohl", ILibDuktape_ntohl, 2);
	ILibDuktape_CreateInstanceMethod(ctx, "ntohs", ILibDuktape_ntohs, 2);
	ILibDuktape_CreateInstanceMethod(ctx, "htonl", ILibDuktape_htonl, 3);
	ILibDuktape_CreateInstanceMethod(ctx, "htons", ILibDuktape_htons, 3);
}

typedef enum ILibDuktape_Timer_Type
{
	ILibDuktape_Timer_Type_TIMEOUT = 0,
	ILibDuktape_Timer_Type_INTERVAL = 1,
	ILibDuktape_Timer_Type_IMMEDIATE = 2
}ILibDuktape_Timer_Type;
typedef struct ILibDuktape_Timer
{
	duk_context *ctx;
	void *object;
	void *callback;
	void *args;
	int timeout;
	ILibDuktape_Timer_Type timerType;
}ILibDuktape_Timer;

duk_ret_t ILibDuktape_Polyfills_timer_finalizer(duk_context *ctx)
{
	// Make sure we remove any timers just in case, so we don't leak resources
	ILibDuktape_Timer *ptrs;
	if (duk_has_prop_string(ctx, 0, ILibDuktape_Timer_Ptrs))
	{
		duk_get_prop_string(ctx, 0, ILibDuktape_Timer_Ptrs);
		if (duk_has_prop_string(ctx, 0, "\xFF_callback"))
		{
			duk_del_prop_string(ctx, 0, "\xFF_callback");
		}
		if (duk_has_prop_string(ctx, 0, "\xFF_argArray"))
		{
			duk_del_prop_string(ctx, 0, "\xFF_argArray");
		}
		ptrs = (ILibDuktape_Timer*)Duktape_GetBuffer(ctx, -1, NULL);

		ILibLifeTime_Remove(ILibGetBaseTimer(Duktape_GetChain(ctx)), ptrs);
	}

	duk_eval_string(ctx, "require('events')");			// [events]
	duk_prepare_method_call(ctx, -1, "deleteProperty");	// [events][deleteProperty][this]
	duk_push_this(ctx);									// [events][deleteProperty][this][timer]
	duk_prepare_method_call(ctx, -4, "hiddenProperties");//[events][deleteProperty][this][timer][hidden][this]
	duk_push_this(ctx);									// [events][deleteProperty][this][timer][hidden][this][timer]
	duk_call_method(ctx, 1);							// [events][deleteProperty][this][timer][array]
	duk_call_method(ctx, 2);							// [events][ret]
	return 0;
}
void ILibDuktape_Polyfills_timer_elapsed(void *obj)
{
	ILibDuktape_Timer *ptrs = (ILibDuktape_Timer*)obj;
	int argCount, i;
	duk_context *ctx = ptrs->ctx;
	char *funcName;

	if (!ILibMemory_CanaryOK(ptrs)) { return; }
	if (duk_check_stack(ctx, 3) == 0) { return; }

	duk_push_heapptr(ctx, ptrs->callback);				// [func]
	funcName = Duktape_GetStringPropertyValue(ctx, -1, "name", "unknown_method");
	duk_push_heapptr(ctx, ptrs->object);				// [func][this]
	duk_push_heapptr(ctx, ptrs->args);					// [func][this][argArray]

	if (ptrs->timerType == ILibDuktape_Timer_Type_INTERVAL)
	{
		char *metadata = ILibLifeTime_GetCurrentTriggeredMetadata(ILibGetBaseTimer(duk_ctx_chain(ctx)));
		ILibLifeTime_AddEx3(ILibGetBaseTimer(Duktape_GetChain(ctx)), ptrs, ptrs->timeout, ILibDuktape_Polyfills_timer_elapsed, NULL, metadata);
	}
	else
	{
		if (ptrs->timerType == ILibDuktape_Timer_Type_IMMEDIATE)
		{
			duk_push_heap_stash(ctx);
			duk_del_prop_string(ctx, -1, Duktape_GetStashKey(ptrs->object));
			duk_pop(ctx);
		}

		duk_del_prop_string(ctx, -2, "\xFF_callback");
		duk_del_prop_string(ctx, -2, "\xFF_argArray");
		duk_del_prop_string(ctx, -2, ILibDuktape_Timer_Ptrs);
	}

	argCount = (int)duk_get_length(ctx, -1);
	for (i = 0; i < argCount; ++i)
	{
		duk_get_prop_index(ctx, -1, i);					// [func][this][argArray][arg]
		duk_swap_top(ctx, -2);							// [func][this][arg][argArray]
	}
	duk_pop(ctx);										// [func][this][...arg...]
	if (duk_pcall_method(ctx, argCount) != 0) { ILibDuktape_Process_UncaughtExceptionEx(ctx, "timers.onElapsed() callback handler on '%s()' ", funcName); }
	duk_pop(ctx);										// ...
}
duk_ret_t ILibDuktape_Polyfills_Timer_Metadata(duk_context *ctx)
{
	duk_push_this(ctx);
	ILibLifeTime_Token token = (ILibLifeTime_Token)Duktape_GetPointerProperty(ctx, -1, "\xFF_token");
	if (token != NULL)
	{
		duk_size_t metadataLen;
		char *metadata = (char*)duk_require_lstring(ctx, 0, &metadataLen);
		ILibLifeTime_SetMetadata(token, metadata, metadataLen);
	}
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_timer_set(duk_context *ctx)
{
	char *metadata = NULL;
	int nargs = duk_get_top(ctx);
	ILibDuktape_Timer *ptrs;
	ILibDuktape_Timer_Type timerType;
	void *chain = Duktape_GetChain(ctx);
	int argx;

	duk_push_current_function(ctx);
	duk_get_prop_string(ctx, -1, "type");
	timerType = (ILibDuktape_Timer_Type)duk_get_int(ctx, -1);

	duk_push_object(ctx);																	//[retVal]
	switch (timerType)
	{
	case ILibDuktape_Timer_Type_IMMEDIATE:
		ILibDuktape_WriteID(ctx, "Timers.immediate");	
		metadata = "setImmediate()";
		// We're only saving a reference for immediates
		duk_push_heap_stash(ctx);															//[retVal][stash]
		duk_dup(ctx, -2);																	//[retVal][stash][immediate]
		duk_put_prop_string(ctx, -2, Duktape_GetStashKey(duk_get_heapptr(ctx, -1)));		//[retVal][stash]
		duk_pop(ctx);																		//[retVal]
		break;
	case ILibDuktape_Timer_Type_INTERVAL:
		ILibDuktape_WriteID(ctx, "Timers.interval");
		metadata = "setInterval()";
		break;
	case ILibDuktape_Timer_Type_TIMEOUT:
		ILibDuktape_WriteID(ctx, "Timers.timeout");
		metadata = "setTimeout()";
		break;
	}
	ILibDuktape_CreateFinalizer(ctx, ILibDuktape_Polyfills_timer_finalizer);
	
	ptrs = (ILibDuktape_Timer*)Duktape_PushBuffer(ctx, sizeof(ILibDuktape_Timer));	//[retVal][ptrs]
	duk_put_prop_string(ctx, -2, ILibDuktape_Timer_Ptrs);							//[retVal]

	ptrs->ctx = ctx;
	ptrs->object = duk_get_heapptr(ctx, -1);
	ptrs->timerType = timerType;
	ptrs->timeout = timerType == ILibDuktape_Timer_Type_IMMEDIATE ? 0 : (int)duk_require_int(ctx, 1);
	ptrs->callback = duk_require_heapptr(ctx, 0);

	duk_push_array(ctx);																			//[retVal][argArray]
	for (argx = ILibDuktape_Timer_Type_IMMEDIATE == timerType ? 1 : 2; argx < nargs; ++argx)
	{
		duk_dup(ctx, argx);																			//[retVal][argArray][arg]
		duk_put_prop_index(ctx, -2, argx - (ILibDuktape_Timer_Type_IMMEDIATE == timerType ? 1 : 2));//[retVal][argArray]
	}
	ptrs->args = duk_get_heapptr(ctx, -1);															//[retVal]
	duk_put_prop_string(ctx, -2, "\xFF_argArray");

	duk_dup(ctx, 0);																				//[retVal][callback]
	duk_put_prop_string(ctx, -2, "\xFF_callback");													//[retVal]

	duk_push_pointer(
		ctx,
		ILibLifeTime_AddEx3(ILibGetBaseTimer(chain), ptrs, ptrs->timeout, ILibDuktape_Polyfills_timer_elapsed, NULL, metadata));
	duk_put_prop_string(ctx, -2, "\xFF_token");
	ILibDuktape_CreateEventWithSetterEx(ctx, "metadata", ILibDuktape_Polyfills_Timer_Metadata);
	return 1;
}
duk_ret_t ILibDuktape_Polyfills_timer_clear(duk_context *ctx)
{
	ILibDuktape_Timer *ptrs;
	ILibDuktape_Timer_Type timerType;
	
	duk_push_current_function(ctx);
	duk_get_prop_string(ctx, -1, "type");
	timerType = (ILibDuktape_Timer_Type)duk_get_int(ctx, -1);

	if(!duk_has_prop_string(ctx, 0, ILibDuktape_Timer_Ptrs)) 
	{
		switch (timerType)
		{
			case ILibDuktape_Timer_Type_TIMEOUT:
				return(ILibDuktape_Error(ctx, "timers.clearTimeout(): Invalid Parameter"));
			case ILibDuktape_Timer_Type_INTERVAL:
				return(ILibDuktape_Error(ctx, "timers.clearInterval(): Invalid Parameter"));
			case ILibDuktape_Timer_Type_IMMEDIATE:
				return(ILibDuktape_Error(ctx, "timers.clearImmediate(): Invalid Parameter"));
		}
	}

	duk_dup(ctx, 0);
	duk_del_prop_string(ctx, -1, "\xFF_argArray");

	duk_get_prop_string(ctx, 0, ILibDuktape_Timer_Ptrs);
	ptrs = (ILibDuktape_Timer*)Duktape_GetBuffer(ctx, -1, NULL);

	if (ptrs->timerType == ILibDuktape_Timer_Type_IMMEDIATE)
	{
		duk_push_heap_stash(ctx);
		duk_del_prop_string(ctx, -1, Duktape_GetStashKey(ptrs->object));
		duk_pop(ctx);
	}

	ILibLifeTime_Remove(ILibGetBaseTimer(Duktape_GetChain(ctx)), ptrs);
	return 0;
}
void ILibDuktape_Polyfills_timer(duk_context *ctx)
{
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_TIMEOUT, "setTimeout", ILibDuktape_Polyfills_timer_set, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_INTERVAL, "setInterval", ILibDuktape_Polyfills_timer_set, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_IMMEDIATE, "setImmediate", ILibDuktape_Polyfills_timer_set, DUK_VARARGS);

	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_TIMEOUT, "clearTimeout", ILibDuktape_Polyfills_timer_clear, 1);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_INTERVAL, "clearInterval", ILibDuktape_Polyfills_timer_clear, 1);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "type", ILibDuktape_Timer_Type_IMMEDIATE, "clearImmediate", ILibDuktape_Polyfills_timer_clear, 1);
}
duk_ret_t ILibDuktape_Polyfills_getJSModule(duk_context *ctx)
{
	if (ILibDuktape_ModSearch_GetJSModule(ctx, (char*)duk_require_string(ctx, 0)) == 0)
	{
		return(ILibDuktape_Error(ctx, "getJSModule(): (%s) not found", (char*)duk_require_string(ctx, 0)));
	}
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_getJSModuleDate(duk_context *ctx)
{
	duk_push_uint(ctx, ILibDuktape_ModSearch_GetJSModuleDate(ctx, (char*)duk_require_string(ctx, 0)));
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_addModule(duk_context *ctx)
{
	int narg = duk_get_top(ctx);
	duk_size_t moduleLen;
	duk_size_t moduleNameLen;
	char *module = (char*)Duktape_GetBuffer(ctx, 1, &moduleLen);
	char *moduleName = (char*)Duktape_GetBuffer(ctx, 0, &moduleNameLen);
	char *mtime = narg > 2 ? (char*)duk_require_string(ctx, 2) : NULL;
	int add = 0;

	ILibDuktape_Polyfills_getJSModuleDate(ctx);								// [existing]
	uint32_t update = 0;
	uint32_t existing = duk_get_uint(ctx, -1);
	duk_pop(ctx);															// ...

	if (mtime != NULL)
	{
		// Check the timestamps
		duk_push_sprintf(ctx, "(new Date('%s')).getTime()/1000", mtime);	// [str]
		duk_eval(ctx);														// [new]
		update = duk_get_uint(ctx, -1);
		duk_pop(ctx);														// ...
	}
	if ((update > existing) || (update == existing && update == 0)) { add = 1; }

	if (add != 0)
	{
		if (ILibDuktape_ModSearch_IsRequired(ctx, moduleName, (int)moduleNameLen) != 0)
		{
			// Module is already cached, so we need to do some magic
			duk_push_sprintf(ctx, "if(global._legacyrequire==null) {global._legacyrequire = global.require; global.require = global._altrequire;}");
			duk_eval_noresult(ctx);
		}
		if (ILibDuktape_ModSearch_AddModuleEx(ctx, moduleName, module, (int)moduleLen, mtime) != 0)
		{
			return(ILibDuktape_Error(ctx, "Cannot add module: %s", moduleName));
		}
	}
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_addCompressedModule_dataSink(duk_context *ctx)
{
	duk_push_this(ctx);								// [stream]
	if (!duk_has_prop_string(ctx, -1, "_buffer"))
	{
		duk_push_array(ctx);						// [stream][array]
		duk_dup(ctx, 0);							// [stream][array][buffer]
		duk_array_push(ctx, -2);					// [stream][array]
		duk_buffer_concat(ctx);						// [stream][buffer]
		duk_put_prop_string(ctx, -2, "_buffer");	// [stream]
	}
	else
	{
		duk_push_array(ctx);						// [stream][array]
		duk_get_prop_string(ctx, -2, "_buffer");	// [stream][array][buffer]
		duk_array_push(ctx, -2);					// [stream][array]
		duk_dup(ctx, 0);							// [stream][array][buffer]
		duk_array_push(ctx, -2);					// [stream][array]
		duk_buffer_concat(ctx);						// [stream][buffer]
		duk_put_prop_string(ctx, -2, "_buffer");	// [stream]
	}
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_addCompressedModule(duk_context *ctx)
{
	int narg = duk_get_top(ctx);
	duk_eval_string(ctx, "require('compressed-stream').createDecompressor();");
	duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, ILibDuktape_EventEmitter_FinalizerDebugMessage);
	void *decoder = duk_get_heapptr(ctx, -1);
	ILibDuktape_EventEmitter_AddOnEx(ctx, -1, "data", ILibDuktape_Polyfills_addCompressedModule_dataSink);

	duk_dup(ctx, -1);						// [stream]
	duk_get_prop_string(ctx, -1, "end");	// [stream][end]
	duk_swap_top(ctx, -2);					// [end][this]
	duk_dup(ctx, 1);						// [end][this][buffer]
	if (duk_pcall_method(ctx, 1) == 0)
	{
		duk_push_heapptr(ctx, decoder);				// [stream]
		duk_get_prop_string(ctx, -1, "_buffer");	// [stream][buffer]
		duk_get_prop_string(ctx, -1, "toString");	// [stream][buffer][toString]
		duk_swap_top(ctx, -2);						// [stream][toString][this]
		duk_call_method(ctx, 0);					// [stream][decodedString]
		duk_push_global_object(ctx);				// [stream][decodedString][global]
		duk_get_prop_string(ctx, -1, "addModule");	// [stream][decodedString][global][addModule]
		duk_swap_top(ctx, -2);						// [stream][decodedString][addModule][this]
		duk_dup(ctx, 0);							// [stream][decodedString][addModule][this][name]
		duk_dup(ctx, -4);							// [stream][decodedString][addModule][this][name][string]
		if (narg > 2) { duk_dup(ctx, 2); }
		duk_pcall_method(ctx, narg);
	}

	duk_push_heapptr(ctx, decoder);							// [stream]
	duk_prepare_method_call(ctx, -1, "removeAllListeners");	// [stream][remove][this]
	duk_pcall_method(ctx, 0);

	return(0);
}
duk_ret_t ILibDuktape_Polyfills_addModuleObject(duk_context *ctx)
{
	void *module = duk_require_heapptr(ctx, 1);
	char *moduleName = (char*)duk_require_string(ctx, 0);

	ILibDuktape_ModSearch_AddModuleObject(ctx, moduleName, module);
	return(0);
}
duk_ret_t ILibDuktape_Queue_Finalizer(duk_context *ctx)
{
	duk_get_prop_string(ctx, 0, ILibDuktape_Queue_Ptr);
	ILibQueue_Destroy((ILibQueue)duk_get_pointer(ctx, -1));
	return(0);
}
duk_ret_t ILibDuktape_Queue_EnQueue(duk_context *ctx)
{
	ILibQueue Q;
	int i;
	int nargs = duk_get_top(ctx);
	duk_push_this(ctx);																// [queue]
	duk_get_prop_string(ctx, -1, ILibDuktape_Queue_Ptr);							// [queue][ptr]
	Q = (ILibQueue)duk_get_pointer(ctx, -1);
	duk_pop(ctx);																	// [queue]

	ILibDuktape_Push_ObjectStash(ctx);												// [queue][stash]
	duk_push_array(ctx);															// [queue][stash][array]
	for (i = 0; i < nargs; ++i)
	{
		duk_dup(ctx, i);															// [queue][stash][array][arg]
		duk_put_prop_index(ctx, -2, i);												// [queue][stash][array]
	}
	ILibQueue_EnQueue(Q, duk_get_heapptr(ctx, -1));
	duk_put_prop_string(ctx, -2, Duktape_GetStashKey(duk_get_heapptr(ctx, -1)));	// [queue][stash]
	return(0);
}
duk_ret_t ILibDuktape_Queue_DeQueue(duk_context *ctx)
{
	duk_push_current_function(ctx);
	duk_get_prop_string(ctx, -1, "peek");
	int peek = duk_get_int(ctx, -1);

	duk_push_this(ctx);										// [Q]
	duk_get_prop_string(ctx, -1, ILibDuktape_Queue_Ptr);	// [Q][ptr]
	ILibQueue Q = (ILibQueue)duk_get_pointer(ctx, -1);
	void *h = peek == 0 ? ILibQueue_DeQueue(Q) : ILibQueue_PeekQueue(Q);
	if (h == NULL) { return(ILibDuktape_Error(ctx, "Queue is empty")); }
	duk_pop(ctx);											// [Q]
	ILibDuktape_Push_ObjectStash(ctx);						// [Q][stash]
	duk_push_heapptr(ctx, h);								// [Q][stash][array]
	int length = (int)duk_get_length(ctx, -1);
	int i;
	for (i = 0; i < length; ++i)
	{
		duk_get_prop_index(ctx, -i - 1, i);				   // [Q][stash][array][args]
	}
	if (peek == 0) { duk_del_prop_string(ctx, -length - 2, Duktape_GetStashKey(h)); }
	return(length);
}
duk_ret_t ILibDuktape_Queue_isEmpty(duk_context *ctx)
{
	duk_push_this(ctx);
	duk_push_boolean(ctx, ILibQueue_IsEmpty((ILibQueue)Duktape_GetPointerProperty(ctx, -1, ILibDuktape_Queue_Ptr)));
	return(1);
}
duk_ret_t ILibDuktape_Queue_new(duk_context *ctx)
{
	duk_push_object(ctx);									// [queue]
	duk_push_pointer(ctx, ILibQueue_Create());				// [queue][ptr]
	duk_put_prop_string(ctx, -2, ILibDuktape_Queue_Ptr);	// [queue]

	ILibDuktape_CreateFinalizer(ctx, ILibDuktape_Queue_Finalizer);
	ILibDuktape_CreateInstanceMethod(ctx, "enQueue", ILibDuktape_Queue_EnQueue, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "peek", 0, "deQueue", ILibDuktape_Queue_DeQueue, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethodWithIntProperty(ctx, "peek", 1, "peekQueue", ILibDuktape_Queue_DeQueue, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "isEmpty", ILibDuktape_Queue_isEmpty, 0);

	return(1);
}
void ILibDuktape_Queue_Push(duk_context *ctx, void* chain)
{
	duk_push_c_function(ctx, ILibDuktape_Queue_new, 0);
}

typedef struct ILibDuktape_DynamicBuffer_data
{
	int start;
	int end;
	int unshiftBytes;
	char *buffer;
	int bufferLen;
}ILibDuktape_DynamicBuffer_data;

typedef struct ILibDuktape_DynamicBuffer_ContextSwitchData
{
	void *chain;
	void *heapptr;
	ILibDuktape_DuplexStream *stream;
	ILibDuktape_DynamicBuffer_data *data;
	int bufferLen;
	char buffer[];
}ILibDuktape_DynamicBuffer_ContextSwitchData;

ILibTransport_DoneState ILibDuktape_DynamicBuffer_WriteSink(ILibDuktape_DuplexStream *stream, char *buffer, int bufferLen, void *user);
void ILibDuktape_DynamicBuffer_WriteSink_ChainThread(void *chain, void *user)
{
	ILibDuktape_DynamicBuffer_ContextSwitchData *data = (ILibDuktape_DynamicBuffer_ContextSwitchData*)user;
	if(ILibMemory_CanaryOK(data->stream))
	{
		ILibDuktape_DynamicBuffer_WriteSink(data->stream, data->buffer, data->bufferLen, data->data);
		ILibDuktape_DuplexStream_Ready(data->stream);
	}
	free(user);
}
ILibTransport_DoneState ILibDuktape_DynamicBuffer_WriteSink(ILibDuktape_DuplexStream *stream, char *buffer, int bufferLen, void *user)
{
	ILibDuktape_DynamicBuffer_data *data = (ILibDuktape_DynamicBuffer_data*)user;
	if (ILibIsRunningOnChainThread(stream->readableStream->chain) == 0)
	{
		ILibDuktape_DynamicBuffer_ContextSwitchData *tmp = (ILibDuktape_DynamicBuffer_ContextSwitchData*)ILibMemory_Allocate(sizeof(ILibDuktape_DynamicBuffer_ContextSwitchData) + bufferLen, 0, NULL, NULL);
		tmp->chain = stream->readableStream->chain;
		tmp->heapptr = stream->ParentObject;
		tmp->stream = stream;
		tmp->data = data;
		tmp->bufferLen = bufferLen;
		memcpy_s(tmp->buffer, bufferLen, buffer, bufferLen);
		Duktape_RunOnEventLoop(tmp->chain, duk_ctx_nonce(stream->readableStream->ctx), stream->readableStream->ctx, ILibDuktape_DynamicBuffer_WriteSink_ChainThread, NULL, tmp);
		return(ILibTransport_DoneState_INCOMPLETE);
	}


	if ((data->bufferLen - data->start - data->end) < bufferLen)
	{
		if (data->end > 0)
		{
			// Move the buffer first
			memmove_s(data->buffer, data->bufferLen, data->buffer + data->start, data->end);
			data->start = 0;
		}
		if ((data->bufferLen - data->end) < bufferLen)
		{
			// Need to resize buffer first
			int tmpSize = data->bufferLen;
			while ((tmpSize - data->end) < bufferLen)
			{
				tmpSize += 4096;
			}
			if ((data->buffer = (char*)realloc(data->buffer, tmpSize)) == NULL) { ILIBCRITICALEXIT(254); }
			data->bufferLen = tmpSize;
		}
	}


	memcpy_s(data->buffer + data->start + data->end, data->bufferLen - data->start - data->end, buffer, bufferLen);
	data->end += bufferLen;

	int unshifted = 0;
	do
	{
		duk_push_heapptr(stream->readableStream->ctx, stream->ParentObject);		// [ds]
		duk_get_prop_string(stream->readableStream->ctx, -1, "emit");				// [ds][emit]
		duk_swap_top(stream->readableStream->ctx, -2);								// [emit][this]
		duk_push_string(stream->readableStream->ctx, "readable");					// [emit][this][readable]
		if (duk_pcall_method(stream->readableStream->ctx, 1) != 0) { ILibDuktape_Process_UncaughtExceptionEx(stream->readableStream->ctx, "DynamicBuffer.WriteSink => readable(): "); }
		duk_pop(stream->readableStream->ctx);										// ...

		ILibDuktape_DuplexStream_WriteData(stream, data->buffer + data->start, data->end);
		if (data->unshiftBytes == 0)
		{
			// All the data was consumed
			data->start = data->end = 0;
		}
		else
		{
			unshifted = (data->end - data->unshiftBytes);
			if (unshifted > 0)
			{
				data->start += unshifted;
				data->end = data->unshiftBytes;
				data->unshiftBytes = 0;
			}
		}
	} while (unshifted != 0);

	return(ILibTransport_DoneState_COMPLETE);
}
void ILibDuktape_DynamicBuffer_EndSink(ILibDuktape_DuplexStream *stream, void *user)
{
	ILibDuktape_DuplexStream_WriteEnd(stream);
}
duk_ret_t ILibDuktape_DynamicBuffer_Finalizer(duk_context *ctx)
{
	duk_get_prop_string(ctx, 0, "\xFF_buffer");
	ILibDuktape_DynamicBuffer_data *data = (ILibDuktape_DynamicBuffer_data*)Duktape_GetBuffer(ctx, -1, NULL);
	free(data->buffer);
	return(0);
}

int ILibDuktape_DynamicBuffer_unshift(ILibDuktape_DuplexStream *sender, int unshiftBytes, void *user)
{
	ILibDuktape_DynamicBuffer_data *data = (ILibDuktape_DynamicBuffer_data*)user;
	data->unshiftBytes = unshiftBytes;
	return(unshiftBytes);
}
duk_ret_t ILibDuktape_DynamicBuffer_read(duk_context *ctx)
{
	ILibDuktape_DynamicBuffer_data *data;
	duk_push_this(ctx);															// [DynamicBuffer]
	duk_get_prop_string(ctx, -1, "\xFF_buffer");								// [DynamicBuffer][buffer]
	data = (ILibDuktape_DynamicBuffer_data*)Duktape_GetBuffer(ctx, -1, NULL);
	duk_push_external_buffer(ctx);												// [DynamicBuffer][buffer][extBuffer]
	duk_config_buffer(ctx, -1, data->buffer + data->start, data->bufferLen - (data->start + data->end));
	duk_push_buffer_object(ctx, -1, 0, data->bufferLen - (data->start + data->end), DUK_BUFOBJ_NODEJS_BUFFER);
	return(1);
}
duk_ret_t ILibDuktape_DynamicBuffer_new(duk_context *ctx)
{
	ILibDuktape_DynamicBuffer_data *data;
	int initSize = 4096;
	if (duk_get_top(ctx) != 0)
	{
		initSize = duk_require_int(ctx, 0);
	}

	duk_push_object(ctx);					// [stream]
	duk_push_fixed_buffer(ctx, sizeof(ILibDuktape_DynamicBuffer_data));
	data = (ILibDuktape_DynamicBuffer_data*)Duktape_GetBuffer(ctx, -1, NULL);
	memset(data, 0, sizeof(ILibDuktape_DynamicBuffer_data));
	duk_put_prop_string(ctx, -2, "\xFF_buffer");

	data->bufferLen = initSize;
	data->buffer = (char*)malloc(initSize);

	ILibDuktape_DuplexStream_InitEx(ctx, ILibDuktape_DynamicBuffer_WriteSink, ILibDuktape_DynamicBuffer_EndSink, NULL, NULL, ILibDuktape_DynamicBuffer_unshift, data);
	ILibDuktape_EventEmitter_CreateEventEx(ILibDuktape_EventEmitter_GetEmitter(ctx, -1), "readable");
	ILibDuktape_CreateInstanceMethod(ctx, "read", ILibDuktape_DynamicBuffer_read, DUK_VARARGS);
	ILibDuktape_CreateFinalizer(ctx, ILibDuktape_DynamicBuffer_Finalizer);

	return(1);
}

void ILibDuktape_DynamicBuffer_Push(duk_context *ctx, void *chain)
{
	duk_push_c_function(ctx, ILibDuktape_DynamicBuffer_new, DUK_VARARGS);
}

duk_ret_t ILibDuktape_Polyfills_debugCrash(duk_context *ctx)
{
	void *p = NULL;
	((int*)p)[0] = 55;
	return(0);
}


void ILibDuktape_Stream_PauseSink(struct ILibDuktape_readableStream *sender, void *user)
{
}
void ILibDuktape_Stream_ResumeSink(struct ILibDuktape_readableStream *sender, void *user)
{
	int skip = 0;
	duk_size_t bufferLen;

	duk_push_heapptr(sender->ctx, sender->object);			// [stream]
	void *func = Duktape_GetHeapptrProperty(sender->ctx, -1, "_read");
	duk_pop(sender->ctx);									// ...

	while (func != NULL && sender->paused == 0)
	{
		duk_push_heapptr(sender->ctx, sender->object);									// [this]
		if (!skip && duk_has_prop_string(sender->ctx, -1, ILibDuktape_Stream_Buffer))
		{
			duk_get_prop_string(sender->ctx, -1, ILibDuktape_Stream_Buffer);			// [this][buffer]
			if ((bufferLen = duk_get_length(sender->ctx, -1)) > 0)
			{
				// Buffer is not empty, so we need to 'PUSH' it
				duk_get_prop_string(sender->ctx, -2, "push");							// [this][buffer][push]
				duk_dup(sender->ctx, -3);												// [this][buffer][push][this]
				duk_dup(sender->ctx, -3);												// [this][buffer][push][this][buffer]
				duk_remove(sender->ctx, -4);											// [this][push][this][buffer]
				duk_call_method(sender->ctx, 1);										// [this][boolean]
				sender->paused = !duk_get_boolean(sender->ctx, -1);
				duk_pop(sender->ctx);													// [this]

				if (duk_has_prop_string(sender->ctx, -1, ILibDuktape_Stream_Buffer))
				{
					duk_get_prop_string(sender->ctx, -1, ILibDuktape_Stream_Buffer);	// [this][buffer]
					if (duk_get_length(sender->ctx, -1) == bufferLen)
					{
						// All the data was unshifted
						skip = !sender->paused;					
					}
					duk_pop(sender->ctx);												// [this]
				}
				duk_pop(sender->ctx);													// ...
			}
			else
			{
				// Buffer is empty
				duk_pop(sender->ctx);													// [this]
				duk_del_prop_string(sender->ctx, -1, ILibDuktape_Stream_Buffer);
				duk_pop(sender->ctx);													// ...
			}
		}
		else
		{
			// We need to 'read' more data
			duk_push_heapptr(sender->ctx, func);										// [this][read]
			duk_swap_top(sender->ctx, -2);												// [read][this]
			if (duk_pcall_method(sender->ctx, 0) != 0) { ILibDuktape_Process_UncaughtException(sender->ctx); duk_pop(sender->ctx); break; }
			//																			// [buffer]
			if (duk_is_null_or_undefined(sender->ctx, -1))
			{
				duk_pop(sender->ctx);
				break;
			}
			duk_push_heapptr(sender->ctx, sender->object);								// [buffer][this]
			duk_swap_top(sender->ctx, -2);												// [this][buffer]
			if (duk_has_prop_string(sender->ctx, -2, ILibDuktape_Stream_Buffer))
			{
				duk_push_global_object(sender->ctx);									// [this][buffer][g]
				duk_get_prop_string(sender->ctx, -1, "Buffer");							// [this][buffer][g][Buffer]
				duk_remove(sender->ctx, -2);											// [this][buffer][Buffer]
				duk_get_prop_string(sender->ctx, -1, "concat");							// [this][buffer][Buffer][concat]
				duk_swap_top(sender->ctx, -2);											// [this][buffer][concat][this]
				duk_push_array(sender->ctx);											// [this][buffer][concat][this][Array]
				duk_get_prop_string(sender->ctx, -1, "push");							// [this][buffer][concat][this][Array][push]
				duk_dup(sender->ctx, -2);												// [this][buffer][concat][this][Array][push][this]
				duk_get_prop_string(sender->ctx, -7, ILibDuktape_Stream_Buffer);		// [this][buffer][concat][this][Array][push][this][buffer]
				duk_call_method(sender->ctx, 1); duk_pop(sender->ctx);					// [this][buffer][concat][this][Array]
				duk_get_prop_string(sender->ctx, -1, "push");							// [this][buffer][concat][this][Array][push]
				duk_dup(sender->ctx, -2);												// [this][buffer][concat][this][Array][push][this]
				duk_dup(sender->ctx, -6);												// [this][buffer][concat][this][Array][push][this][buffer]
				duk_remove(sender->ctx, -7);											// [this][concat][this][Array][push][this][buffer]
				duk_call_method(sender->ctx, 1); duk_pop(sender->ctx);					// [this][concat][this][Array]
				duk_call_method(sender->ctx, 1);										// [this][buffer]
			}
			duk_put_prop_string(sender->ctx, -2, ILibDuktape_Stream_Buffer);			// [this]
			duk_pop(sender->ctx);														// ...
			skip = 0;
		}
	}
}
int ILibDuktape_Stream_UnshiftSink(struct ILibDuktape_readableStream *sender, int unshiftBytes, void *user)
{
	duk_push_fixed_buffer(sender->ctx, unshiftBytes);									// [buffer]
	memcpy_s(Duktape_GetBuffer(sender->ctx, -1, NULL), unshiftBytes, sender->unshiftReserved, unshiftBytes);
	duk_push_heapptr(sender->ctx, sender->object);										// [buffer][stream]
	duk_push_buffer_object(sender->ctx, -2, 0, unshiftBytes, DUK_BUFOBJ_NODEJS_BUFFER);	// [buffer][stream][buffer]
	duk_put_prop_string(sender->ctx, -2, ILibDuktape_Stream_Buffer);					// [buffer][stream]
	duk_pop_2(sender->ctx);																// ...

	return(unshiftBytes);
}
duk_ret_t ILibDuktape_Stream_Push(duk_context *ctx)
{
	duk_push_this(ctx);																					// [stream]

	ILibDuktape_readableStream *RS = (ILibDuktape_readableStream*)Duktape_GetPointerProperty(ctx, -1, ILibDuktape_Stream_ReadablePtr);

	duk_size_t bufferLen;
	char *buffer = (char*)Duktape_GetBuffer(ctx, 0, &bufferLen);
	if (buffer != NULL)
	{
		duk_push_boolean(ctx, !ILibDuktape_readableStream_WriteDataEx(RS, 0, buffer, (int)bufferLen));		// [stream][buffer][retVal]
	}
	else
	{
		ILibDuktape_readableStream_WriteEnd(RS);
		duk_push_false(ctx);
	}
	return(1);
}
duk_ret_t ILibDuktape_Stream_EndSink(duk_context *ctx)
{
	duk_push_this(ctx);												// [stream]
	ILibDuktape_readableStream *RS = (ILibDuktape_readableStream*)Duktape_GetPointerProperty(ctx, -1, ILibDuktape_Stream_ReadablePtr);
	ILibDuktape_readableStream_WriteEnd(RS);
	return(0);
}
duk_ret_t ILibDuktape_Stream_readonlyError(duk_context *ctx)
{
	duk_push_current_function(ctx);
	duk_size_t len;
	char *propName = Duktape_GetStringPropertyValueEx(ctx, -1, "propName", "<unknown>", &len);
	duk_push_lstring(ctx, propName, len);
	duk_get_prop_string(ctx, -1, "concat");					// [string][concat]
	duk_swap_top(ctx, -2);									// [concat][this]
	duk_push_string(ctx, " is readonly");					// [concat][this][str]
	duk_call_method(ctx, 1);								// [str]
	duk_throw(ctx);
	return(0);
}
duk_idx_t ILibDuktape_Stream_newReadable(duk_context *ctx)
{
	ILibDuktape_readableStream *RS;
	duk_push_object(ctx);							// [Readable]
	ILibDuktape_WriteID(ctx, "stream.readable");
	RS = ILibDuktape_ReadableStream_InitEx(ctx, ILibDuktape_Stream_PauseSink, ILibDuktape_Stream_ResumeSink, ILibDuktape_Stream_UnshiftSink, NULL);
	RS->paused = 1;

	duk_push_pointer(ctx, RS);
	duk_put_prop_string(ctx, -2, ILibDuktape_Stream_ReadablePtr);
	ILibDuktape_CreateInstanceMethod(ctx, "push", ILibDuktape_Stream_Push, DUK_VARARGS);
	ILibDuktape_EventEmitter_AddOnceEx3(ctx, -1, "end", ILibDuktape_Stream_EndSink);

	if (duk_is_object(ctx, 0))
	{
		void *h = Duktape_GetHeapptrProperty(ctx, 0, "read");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_read"); }
		else
		{
			ILibDuktape_CreateEventWithSetterEx(ctx, "_read", ILibDuktape_Stream_readonlyError);
		}
	}
	return(1);
}
duk_ret_t ILibDuktape_Stream_Writable_WriteSink_Flush(duk_context *ctx)
{
	duk_push_current_function(ctx);
	ILibTransport_DoneState *retVal = (ILibTransport_DoneState*)Duktape_GetPointerProperty(ctx, -1, "retval");
	if (retVal != NULL)
	{
		*retVal = ILibTransport_DoneState_COMPLETE;
	}
	else
	{
		ILibDuktape_WritableStream *WS = (ILibDuktape_WritableStream*)Duktape_GetPointerProperty(ctx, -1, ILibDuktape_Stream_WritablePtr);
		ILibDuktape_WritableStream_Ready(WS);
	}
	return(0);
}
ILibTransport_DoneState ILibDuktape_Stream_Writable_WriteSink(struct ILibDuktape_WritableStream *stream, char *buffer, int bufferLen, void *user)
{
	void *h;
	ILibTransport_DoneState retVal = ILibTransport_DoneState_INCOMPLETE;
	duk_push_this(stream->ctx);																		// [writable]
	int bufmode = Duktape_GetIntPropertyValue(stream->ctx, -1, "bufferMode", 0);
	duk_get_prop_string(stream->ctx, -1, "_write");													// [writable][_write]
	duk_swap_top(stream->ctx, -2);																	// [_write][this]
	if(duk_stream_flags_isBuffer(stream->Reserved))
	{
		if (bufmode == 0)
		{
			// Legacy Mode. We use an external buffer, so a memcpy does not occur. JS must copy memory if it needs to save it
			duk_push_external_buffer(stream->ctx);													// [_write][this][extBuffer]
			duk_config_buffer(stream->ctx, -1, buffer, (duk_size_t)bufferLen);
		}
		else
		{
			// Compliant Mode. We copy the buffer into a buffer that will be wholly owned by the recipient
			char *cb = (char*)duk_push_fixed_buffer(stream->ctx, (duk_size_t)bufferLen);			// [_write][this][extBuffer]
			memcpy_s(cb, (size_t)bufferLen, buffer, (size_t)bufferLen);
		}
		duk_push_buffer_object(stream->ctx, -1, 0, (duk_size_t)bufferLen, DUK_BUFOBJ_NODEJS_BUFFER);// [_write][this][extBuffer][buffer]
		duk_remove(stream->ctx, -2);																// [_write][this][buffer]	
	}
	else
	{
		duk_push_lstring(stream->ctx, buffer, (duk_size_t)bufferLen);								// [_write][this][string]
	}
	duk_push_c_function(stream->ctx, ILibDuktape_Stream_Writable_WriteSink_Flush, DUK_VARARGS);		// [_write][this][string/buffer][callback]
	h = duk_get_heapptr(stream->ctx, -1);
	duk_push_heap_stash(stream->ctx);																// [_write][this][string/buffer][callback][stash]
	duk_dup(stream->ctx, -2);																		// [_write][this][string/buffer][callback][stash][callback]
	duk_put_prop_string(stream->ctx, -2, Duktape_GetStashKey(h));									// [_write][this][string/buffer][callback][stash]
	duk_pop(stream->ctx);																			// [_write][this][string/buffer][callback]
	duk_push_pointer(stream->ctx, stream); duk_put_prop_string(stream->ctx, -2, ILibDuktape_Stream_WritablePtr);

	duk_push_pointer(stream->ctx, &retVal);															// [_write][this][string/buffer][callback][retval]
	duk_put_prop_string(stream->ctx, -2, "retval");													// [_write][this][string/buffer][callback]
	if (duk_pcall_method(stream->ctx, 2) != 0)
	{
		ILibDuktape_Process_UncaughtExceptionEx(stream->ctx, "stream.writable.write(): "); retVal = ILibTransport_DoneState_ERROR;
	}
	else
	{
		if (retVal != ILibTransport_DoneState_COMPLETE)
		{
			retVal = duk_to_boolean(stream->ctx, -1) ? ILibTransport_DoneState_COMPLETE : ILibTransport_DoneState_INCOMPLETE;
		}
	}
	duk_pop(stream->ctx);																			// ...

	duk_push_heapptr(stream->ctx, h);																// [callback]
	duk_del_prop_string(stream->ctx, -1, "retval");
	duk_pop(stream->ctx);																			// ...
	
	duk_push_heap_stash(stream->ctx);
	duk_del_prop_string(stream->ctx, -1, Duktape_GetStashKey(h));
	duk_pop(stream->ctx);
	return(retVal);
}
duk_ret_t ILibDuktape_Stream_Writable_EndSink_finish(duk_context *ctx)
{
	duk_push_current_function(ctx);
	ILibDuktape_WritableStream *ws = (ILibDuktape_WritableStream*)Duktape_GetPointerProperty(ctx, -1, "ptr");
	if (ILibMemory_CanaryOK(ws))
	{
		ILibDuktape_WritableStream_Finish(ws);
	}
	return(0);
}
void ILibDuktape_Stream_Writable_EndSink(struct ILibDuktape_WritableStream *stream, void *user)
{
	duk_push_this(stream->ctx);															// [writable]
	duk_get_prop_string(stream->ctx, -1, "_final");										// [writable][_final]
	duk_swap_top(stream->ctx, -2);														// [_final][this]
	duk_push_c_function(stream->ctx, ILibDuktape_Stream_Writable_EndSink_finish, 0);	// [_final][this][callback]
	duk_push_pointer(stream->ctx, stream); duk_put_prop_string(stream->ctx, -2, "ptr");
	if (duk_pcall_method(stream->ctx, 1) != 0) { ILibDuktape_Process_UncaughtExceptionEx(stream->ctx, "stream.writable._final(): "); }
	duk_pop(stream->ctx);								// ...
}
duk_ret_t ILibDuktape_Stream_newWritable(duk_context *ctx)
{
	ILibDuktape_WritableStream *WS;
	duk_push_object(ctx);						// [Writable]
	ILibDuktape_WriteID(ctx, "stream.writable");
	WS = ILibDuktape_WritableStream_Init(ctx, ILibDuktape_Stream_Writable_WriteSink, ILibDuktape_Stream_Writable_EndSink, NULL);
	WS->JSCreated = 1;

	duk_push_pointer(ctx, WS);
	duk_put_prop_string(ctx, -2, ILibDuktape_Stream_WritablePtr);

	if (duk_is_object(ctx, 0))
	{
		void *h = Duktape_GetHeapptrProperty(ctx, 0, "write");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_write"); }
		h = Duktape_GetHeapptrProperty(ctx, 0, "final");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_final"); }
	}
	return(1);
}
void ILibDuktape_Stream_Duplex_PauseSink(ILibDuktape_DuplexStream *stream, void *user)
{
	ILibDuktape_Stream_PauseSink(stream->readableStream, user);
}
void ILibDuktape_Stream_Duplex_ResumeSink(ILibDuktape_DuplexStream *stream, void *user)
{
	ILibDuktape_Stream_ResumeSink(stream->readableStream, user);
}
int ILibDuktape_Stream_Duplex_UnshiftSink(ILibDuktape_DuplexStream *stream, int unshiftBytes, void *user)
{
	return(ILibDuktape_Stream_UnshiftSink(stream->readableStream, unshiftBytes, user));
}
ILibTransport_DoneState ILibDuktape_Stream_Duplex_WriteSink(ILibDuktape_DuplexStream *stream, char *buffer, int bufferLen, void *user)
{
	return(ILibDuktape_Stream_Writable_WriteSink(stream->writableStream, buffer, bufferLen, user));
}
void ILibDuktape_Stream_Duplex_EndSink(ILibDuktape_DuplexStream *stream, void *user)
{
	ILibDuktape_Stream_Writable_EndSink(stream->writableStream, user);
}

duk_ret_t ILibDuktape_Stream_newDuplex(duk_context *ctx)
{
	ILibDuktape_DuplexStream *DS;
	duk_push_object(ctx);						// [Duplex]
	ILibDuktape_WriteID(ctx, "stream.Duplex");
	DS = ILibDuktape_DuplexStream_InitEx(ctx, ILibDuktape_Stream_Duplex_WriteSink, ILibDuktape_Stream_Duplex_EndSink, ILibDuktape_Stream_Duplex_PauseSink, ILibDuktape_Stream_Duplex_ResumeSink, ILibDuktape_Stream_Duplex_UnshiftSink, NULL);
	DS->writableStream->JSCreated = 1;

	duk_push_pointer(ctx, DS->writableStream);
	duk_put_prop_string(ctx, -2, ILibDuktape_Stream_WritablePtr);

	duk_push_pointer(ctx, DS->readableStream);
	duk_put_prop_string(ctx, -2, ILibDuktape_Stream_ReadablePtr);
	ILibDuktape_CreateInstanceMethod(ctx, "push", ILibDuktape_Stream_Push, DUK_VARARGS);
	ILibDuktape_EventEmitter_AddOnceEx3(ctx, -1, "end", ILibDuktape_Stream_EndSink);

	if (duk_is_object(ctx, 0))
	{
		void *h = Duktape_GetHeapptrProperty(ctx, 0, "write");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_write"); }
		else
		{
			ILibDuktape_CreateEventWithSetterEx(ctx, "_write", ILibDuktape_Stream_readonlyError);
		}
		h = Duktape_GetHeapptrProperty(ctx, 0, "final");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_final"); }
		else
		{
			ILibDuktape_CreateEventWithSetterEx(ctx, "_final", ILibDuktape_Stream_readonlyError);
		}
		h = Duktape_GetHeapptrProperty(ctx, 0, "read");
		if (h != NULL) { duk_push_heapptr(ctx, h); duk_put_prop_string(ctx, -2, "_read"); }
		else
		{
			ILibDuktape_CreateEventWithSetterEx(ctx, "_read", ILibDuktape_Stream_readonlyError);
		}
	}
	return(1);
}
void ILibDuktape_Stream_Init(duk_context *ctx, void *chain)
{
	duk_push_object(ctx);					// [stream
	ILibDuktape_WriteID(ctx, "stream");
	ILibDuktape_CreateInstanceMethod(ctx, "Readable", ILibDuktape_Stream_newReadable, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "Writable", ILibDuktape_Stream_newWritable, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "Duplex", ILibDuktape_Stream_newDuplex, DUK_VARARGS);
}
void ILibDuktape_Polyfills_debugGC2(duk_context *ctx, void ** args, int argsLen)
{
	if (duk_ctx_is_alive((duk_context*)args[1]) && duk_ctx_is_valid((uintptr_t)args[2], ctx) && duk_ctx_shutting_down(ctx)==0)
	{
		if (g_displayFinalizerMessages) { printf("=> GC();\n"); }
		duk_gc(ctx, 0);
	}
}
duk_ret_t ILibDuktape_Polyfills_debugGC(duk_context *ctx)
{
	ILibDuktape_Immediate(ctx, (void*[]) { Duktape_GetChain(ctx), ctx, (void*)duk_ctx_nonce(ctx), NULL }, 3, ILibDuktape_Polyfills_debugGC2);
	return(0);
}
duk_ret_t ILibDuktape_Polyfills_debug(duk_context *ctx)
{
#ifdef WIN32
	if (IsDebuggerPresent()) { __debugbreak(); }
#elif defined(_POSIX)
	raise(SIGTRAP);
#endif
	return(0);
}
#ifndef MICROSTACK_NOTLS
duk_ret_t ILibDuktape_PKCS7_getSignedDataBlock(duk_context *ctx)
{
	char *hash = ILibMemory_AllocateA(UTIL_SHA256_HASHSIZE);
	char *pkeyHash = ILibMemory_AllocateA(UTIL_SHA256_HASHSIZE);
	unsigned int size, r;
	BIO *out = NULL;
	PKCS7 *message = NULL;
	char* data2 = NULL;
	STACK_OF(X509) *st = NULL;

	duk_size_t bufferLen;
	char *buffer = Duktape_GetBuffer(ctx, 0, &bufferLen);

	message = d2i_PKCS7(NULL, (const unsigned char**)&buffer, (long)bufferLen);
	if (message == NULL) { return(ILibDuktape_Error(ctx, "PKCS7 Error")); }

	// Lets rebuild the original message and check the size
	size = i2d_PKCS7(message, NULL);
	if (size < (unsigned int)bufferLen) { PKCS7_free(message); return(ILibDuktape_Error(ctx, "PKCS7 Error")); }

	out = BIO_new(BIO_s_mem());

	// Check the PKCS7 signature, but not the certificate chain.
	r = PKCS7_verify(message, NULL, NULL, NULL, out, PKCS7_NOVERIFY);
	if (r == 0) { PKCS7_free(message); BIO_free(out); return(ILibDuktape_Error(ctx, "PKCS7 Verify Error")); }

	// If data block contains less than 32 bytes, fail.
	size = (unsigned int)BIO_get_mem_data(out, &data2);
	if (size <= ILibMemory_AllocateA_Size(hash)) { PKCS7_free(message); BIO_free(out); return(ILibDuktape_Error(ctx, "PKCS7 Size Mismatch Error")); }


	duk_push_object(ctx);												// [val]
	duk_push_fixed_buffer(ctx, size);									// [val][fbuffer]
	duk_dup(ctx, -1);													// [val][fbuffer][dup]
	duk_put_prop_string(ctx, -3, "\xFF_fixedbuffer");					// [val][fbuffer]
	duk_swap_top(ctx, -2);												// [fbuffer][val]
	duk_push_buffer_object(ctx, -2, 0, size, DUK_BUFOBJ_NODEJS_BUFFER); // [fbuffer][val][buffer]
	ILibDuktape_CreateReadonlyProperty(ctx, "data");					// [fbuffer][val]
	memcpy_s(Duktape_GetBuffer(ctx, -2, NULL), size, data2, size);


	// Get the certificate signer
	st = PKCS7_get0_signers(message, NULL, PKCS7_NOVERIFY);
	
	// Get a full certificate hash of the signer
	X509_digest(sk_X509_value(st, 0), EVP_sha256(), (unsigned char*)hash, NULL);
	X509_pubkey_digest(sk_X509_value(st, 0), EVP_sha256(), (unsigned char*)pkeyHash, NULL); 

	sk_X509_free(st);
	
	// Check certificate hash with first 32 bytes of data.
	if (memcmp(hash, Duktape_GetBuffer(ctx, -2, NULL), ILibMemory_AllocateA_Size(hash)) != 0) { PKCS7_free(message); BIO_free(out); return(ILibDuktape_Error(ctx, "PKCS7 Certificate Hash Mismatch Error")); }
	char *tmp = ILibMemory_AllocateA(1 + (ILibMemory_AllocateA_Size(hash) * 2));
	util_tohex(hash, (int)ILibMemory_AllocateA_Size(hash), tmp);
	duk_push_object(ctx);												// [fbuffer][val][cert]
	ILibDuktape_WriteID(ctx, "certificate");
	duk_push_string(ctx, tmp);											// [fbuffer][val][cert][fingerprint]
	ILibDuktape_CreateReadonlyProperty(ctx, "fingerprint");				// [fbuffer][val][cert]
	util_tohex(pkeyHash, (int)ILibMemory_AllocateA_Size(pkeyHash), tmp);
	duk_push_string(ctx, tmp);											// [fbuffer][val][cert][publickeyhash]
	ILibDuktape_CreateReadonlyProperty(ctx, "publicKeyHash");			// [fbuffer][val][cert]

	ILibDuktape_CreateReadonlyProperty(ctx, "signingCertificate");		// [fbuffer][val]

	// Approved, cleanup and return.
	BIO_free(out);
	PKCS7_free(message);

	return(1);
}
duk_ret_t ILibDuktape_PKCS7_signDataBlockFinalizer(duk_context *ctx)
{
	char *buffer = Duktape_GetPointerProperty(ctx, 0, "\xFF_signature");
	if (buffer != NULL) { free(buffer); }
	return(0);
}
duk_ret_t ILibDuktape_PKCS7_signDataBlock(duk_context *ctx)
{
	duk_get_prop_string(ctx, 1, "secureContext");
	duk_get_prop_string(ctx, -1, "\xFF_SecureContext2CertBuffer");
	struct util_cert *cert = (struct util_cert*)Duktape_GetBuffer(ctx, -1, NULL);
	duk_size_t bufferLen;
	char *buffer = (char*)Duktape_GetBuffer(ctx, 0, &bufferLen);

	BIO *in = NULL;
	PKCS7 *message = NULL;
	char *signature = NULL;
	int signatureLength = 0;

	// Sign the block
	in = BIO_new_mem_buf(buffer, (int)bufferLen);
	message = PKCS7_sign(cert->x509, cert->pkey, NULL, in, PKCS7_BINARY);
	if (message != NULL)
	{
		signatureLength = i2d_PKCS7(message, (unsigned char**)&signature);
		PKCS7_free(message);
	}
	if (in != NULL) BIO_free(in);
	if (signatureLength <= 0) { return(ILibDuktape_Error(ctx, "PKCS7_signDataBlockError: ")); }

	duk_push_external_buffer(ctx);
	duk_config_buffer(ctx, -1, signature, signatureLength);
	duk_push_buffer_object(ctx, -1, 0, signatureLength, DUK_BUFOBJ_NODEJS_BUFFER);
	duk_push_pointer(ctx, signature);
	duk_put_prop_string(ctx, -2, "\xFF_signature");
	ILibDuktape_CreateFinalizer(ctx, ILibDuktape_PKCS7_signDataBlockFinalizer);

	return(1);
}
void ILibDuktape_PKCS7_Push(duk_context *ctx, void *chain)
{
	duk_push_object(ctx);
	ILibDuktape_CreateInstanceMethod(ctx, "getSignedDataBlock", ILibDuktape_PKCS7_getSignedDataBlock, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "signDataBlock", ILibDuktape_PKCS7_signDataBlock, DUK_VARARGS);
}

extern uint32_t crc32c(uint32_t crc, const unsigned char* buf, uint32_t len);
extern uint32_t crc32(uint32_t crc, const unsigned char* buf, uint32_t len);
duk_ret_t ILibDuktape_Polyfills_crc32c(duk_context *ctx)
{
	duk_size_t len;
	char *buffer = Duktape_GetBuffer(ctx, 0, &len);
	uint32_t pre = duk_is_number(ctx, 1) ? duk_require_uint(ctx, 1) : 0;
	duk_push_uint(ctx, crc32c(pre, (unsigned char*)buffer, (uint32_t)len));
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_crc32(duk_context *ctx)
{
	duk_size_t len;
	char *buffer = Duktape_GetBuffer(ctx, 0, &len);
	uint32_t pre = duk_is_number(ctx, 1) ? duk_require_uint(ctx, 1) : 0;
	duk_push_uint(ctx, crc32(pre, (unsigned char*)buffer, (uint32_t)len));
	return(1);
}
#endif
duk_ret_t ILibDuktape_Polyfills_Object_hashCode(duk_context *ctx)
{
	duk_push_this(ctx);
	duk_push_string(ctx, Duktape_GetStashKey(duk_get_heapptr(ctx, -1)));
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Array_peek(duk_context *ctx)
{
	duk_push_this(ctx);				// [Array]
	duk_get_prop_index(ctx, -1, (duk_uarridx_t)duk_get_length(ctx, -1) - 1);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_Object_keys(duk_context *ctx)
{
	duk_push_this(ctx);														// [obj]
	duk_push_array(ctx);													// [obj][keys]
	duk_enum(ctx, -2, DUK_ENUM_OWN_PROPERTIES_ONLY);						// [obj][keys][enum]
	while (duk_next(ctx, -1, 0))											// [obj][keys][enum][key]
	{
		duk_array_push(ctx, -3);											// [obj][keys][enum]
	}
	duk_pop(ctx);															// [obj][keys]
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_function_getter(duk_context *ctx)
{
	duk_push_this(ctx);			// [Function]
	duk_push_true(ctx);
	duk_put_prop_string(ctx, -2, ILibDuktape_EventEmitter_InfrastructureEvent);
	return(1);
}
void ILibDuktape_Polyfills_function(duk_context *ctx)
{
	duk_get_prop_string(ctx, -1, "Function");										// [g][Function]
	duk_get_prop_string(ctx, -1, "prototype");										// [g][Function][prototype]
	ILibDuktape_CreateEventWithGetter(ctx, "internal", ILibDuktape_Polyfills_function_getter);
	duk_pop_2(ctx);																	// [g]
}
void ILibDuktape_Polyfills_object(duk_context *ctx)
{
	// Polyfill Object._hashCode() 
	duk_get_prop_string(ctx, -1, "Object");											// [g][Object]
	duk_get_prop_string(ctx, -1, "prototype");										// [g][Object][prototype]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Object_hashCode, 0);				// [g][Object][prototype][func]
	ILibDuktape_CreateReadonlyProperty(ctx, "_hashCode");							// [g][Object][prototype]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Object_keys, 0);					// [g][Object][prototype][func]
	ILibDuktape_CreateReadonlyProperty(ctx, "keys");								// [g][Object][prototype]
	duk_pop_2(ctx);																	// [g]

	duk_get_prop_string(ctx, -1, "Array");											// [g][Array]
	duk_get_prop_string(ctx, -1, "prototype");										// [g][Array][prototype]
	duk_push_c_function(ctx, ILibDuktape_Polyfills_Array_peek, 0);					// [g][Array][prototype][peek]
	ILibDuktape_CreateReadonlyProperty(ctx, "peek");								// [g][Array][prototype]
	duk_pop_2(ctx);																	// [g]
}


#ifndef MICROSTACK_NOTLS
void ILibDuktape_bignum_addBigNumMethods(duk_context *ctx, BIGNUM *b);
duk_ret_t ILibDuktape_bignum_toString(duk_context *ctx)
{
	duk_push_this(ctx);
	BIGNUM *b = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	if (b != NULL)
	{
		char *numstr = BN_bn2dec(b);
		duk_push_string(ctx, numstr);
		OPENSSL_free(numstr);
		return(1);
	}
	else
	{
		return(ILibDuktape_Error(ctx, "Invalid BIGNUM"));
	}
}
duk_ret_t ILibDuktape_bignum_add(duk_context* ctx)
{
	BIGNUM *ret = BN_new();
	BIGNUM *r1, *r2;

	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");

	BN_add(ret, r1, r2);
	ILibDuktape_bignum_addBigNumMethods(ctx, ret);
	return(1);
}
duk_ret_t ILibDuktape_bignum_sub(duk_context* ctx)
{
	BIGNUM *ret = BN_new();
	BIGNUM *r1, *r2;

	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");

	BN_sub(ret, r1, r2);
	ILibDuktape_bignum_addBigNumMethods(ctx, ret);
	return(1);
}
duk_ret_t ILibDuktape_bignum_mul(duk_context* ctx)
{
	BN_CTX *bx = BN_CTX_new();
	BIGNUM *ret = BN_new();
	BIGNUM *r1, *r2;

	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	BN_mul(ret, r1, r2, bx);
	BN_CTX_free(bx);
	ILibDuktape_bignum_addBigNumMethods(ctx, ret);
	return(1);
}
duk_ret_t ILibDuktape_bignum_div(duk_context* ctx)
{
	BN_CTX *bx = BN_CTX_new();
	BIGNUM *ret = BN_new();
	BIGNUM *r1, *r2;

	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	BN_div(ret, NULL, r1, r2, bx);

	BN_CTX_free(bx);
	ILibDuktape_bignum_addBigNumMethods(ctx, ret);
	return(1);
}
duk_ret_t ILibDuktape_bignum_mod(duk_context* ctx)
{
	BN_CTX *bx = BN_CTX_new();
	BIGNUM *ret = BN_new();
	BIGNUM *r1, *r2;

	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	BN_div(NULL, ret, r1, r2, bx);

	BN_CTX_free(bx);
	ILibDuktape_bignum_addBigNumMethods(ctx, ret);
	return(1);
}
duk_ret_t ILibDuktape_bignum_cmp(duk_context *ctx)
{
	BIGNUM *r1, *r2;
	duk_push_this(ctx);
	r1 = (BIGNUM*)Duktape_GetPointerProperty(ctx, -1, "\xFF_BIGNUM");
	r2 = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	duk_push_int(ctx, BN_cmp(r2, r1));
	return(1);
}

duk_ret_t ILibDuktape_bignum_finalizer(duk_context *ctx)
{
	BIGNUM *b = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	if (b != NULL)
	{
		BN_free(b);
	}
	return(0);
}
void ILibDuktape_bignum_addBigNumMethods(duk_context *ctx, BIGNUM *b)
{
	duk_push_object(ctx);
	duk_push_pointer(ctx, b); duk_put_prop_string(ctx, -2, "\xFF_BIGNUM");
	ILibDuktape_CreateProperty_InstanceMethod(ctx, "toString", ILibDuktape_bignum_toString, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "add", ILibDuktape_bignum_add, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "sub", ILibDuktape_bignum_sub, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "mul", ILibDuktape_bignum_mul, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "div", ILibDuktape_bignum_div, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "mod", ILibDuktape_bignum_mod, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "cmp", ILibDuktape_bignum_cmp, 1);

	duk_push_c_function(ctx, ILibDuktape_bignum_finalizer, 1); duk_set_finalizer(ctx, -2);
	duk_eval_string(ctx, "(function toNumber(){return(parseInt(this.toString()));})"); duk_put_prop_string(ctx, -2, "toNumber");
}
duk_ret_t ILibDuktape_bignum_random(duk_context *ctx)
{
	BIGNUM *r = (BIGNUM*)Duktape_GetPointerProperty(ctx, 0, "\xFF_BIGNUM");
	BIGNUM *rnd = BN_new();

	if (BN_rand_range(rnd, r) == 0) { return(ILibDuktape_Error(ctx, "Error Generating Random Number")); }
	ILibDuktape_bignum_addBigNumMethods(ctx, rnd);
	return(1);
}
duk_ret_t ILibDuktape_bignum_fromBuffer(duk_context *ctx)
{
	char *endian = duk_get_top(ctx) > 1 ? Duktape_GetStringPropertyValue(ctx, 1, "endian", "big") : "big";
	duk_size_t len;
	char *buffer = Duktape_GetBuffer(ctx, 0, &len);
	BIGNUM *b;

	if (strcmp(endian, "big") == 0)
	{
		b = BN_bin2bn((unsigned char*)buffer, (int)len, NULL);
	}
	else if (strcmp(endian, "little") == 0)
	{
#ifdef OLDSSL
		return(ILibDuktape_Error(ctx, "Invalid endian specified"));
#endif
		b = BN_lebin2bn((unsigned char*)buffer, (int)len, NULL);
	}
	else
	{
		return(ILibDuktape_Error(ctx, "Invalid endian specified"));
	}

	ILibDuktape_bignum_addBigNumMethods(ctx, b);
	return(1);
}

duk_ret_t ILibDuktape_bignum_func(duk_context *ctx)
{	
	BIGNUM *b = NULL;
	BN_dec2bn(&b, duk_require_string(ctx, 0));
	ILibDuktape_bignum_addBigNumMethods(ctx, b);
	return(1);
}
void ILibDuktape_bignum_Push(duk_context *ctx, void *chain)
{
	duk_push_c_function(ctx, ILibDuktape_bignum_func, DUK_VARARGS);
	duk_push_c_function(ctx, ILibDuktape_bignum_fromBuffer, DUK_VARARGS); duk_put_prop_string(ctx, -2, "fromBuffer");
	duk_push_c_function(ctx, ILibDuktape_bignum_random, DUK_VARARGS); duk_put_prop_string(ctx, -2, "random");
	
	char randRange[] = "exports.randomRange = function randomRange(low, high)\
						{\
							var result = exports.random(high.sub(low)).add(low);\
							return(result);\
						};";
	ILibDuktape_ModSearch_AddHandler_AlsoIncludeJS(ctx, randRange, sizeof(randRange) - 1);
}
void ILibDuktape_dataGenerator_onPause(struct ILibDuktape_readableStream *sender, void *user)
{

}
void ILibDuktape_dataGenerator_onResume(struct ILibDuktape_readableStream *sender, void *user)
{
	SHA256_CTX shctx;

	char *buffer = (char*)user;
	size_t bufferLen = ILibMemory_Size(buffer);
	int val;

	while (sender->paused == 0)
	{
		duk_push_heapptr(sender->ctx, sender->object);
		val = Duktape_GetIntPropertyValue(sender->ctx, -1, "\xFF_counter", 0);
		duk_push_int(sender->ctx, (val + 1) < 255 ? (val+1) : 0); duk_put_prop_string(sender->ctx, -2, "\xFF_counter");
		duk_pop(sender->ctx);

		//util_random((int)(bufferLen - UTIL_SHA256_HASHSIZE), buffer + UTIL_SHA256_HASHSIZE);
		memset(buffer + UTIL_SHA256_HASHSIZE, val, bufferLen - UTIL_SHA256_HASHSIZE);


		SHA256_Init(&shctx);
		SHA256_Update(&shctx, buffer + UTIL_SHA256_HASHSIZE, bufferLen - UTIL_SHA256_HASHSIZE);
		SHA256_Final((unsigned char*)buffer, &shctx);
		ILibDuktape_readableStream_WriteData(sender, buffer, (int)bufferLen);
	}
}
duk_ret_t ILibDuktape_dataGenerator_const(duk_context *ctx)
{
	int bufSize = (int)duk_require_int(ctx, 0);
	void *buffer;

	if (bufSize <= UTIL_SHA256_HASHSIZE)
	{
		return(ILibDuktape_Error(ctx, "Value too small. Must be > %d", UTIL_SHA256_HASHSIZE));
	}

	duk_push_object(ctx);
	duk_push_int(ctx, 0); duk_put_prop_string(ctx, -2, "\xFF_counter");
	buffer = Duktape_PushBuffer(ctx, bufSize);
	duk_put_prop_string(ctx, -2, "\xFF_buffer");
	ILibDuktape_ReadableStream_Init(ctx, ILibDuktape_dataGenerator_onPause, ILibDuktape_dataGenerator_onResume, buffer)->paused = 1;
	return(1);
}
void ILibDuktape_dataGenerator_Push(duk_context *ctx, void *chain)
{
	duk_push_c_function(ctx, ILibDuktape_dataGenerator_const, DUK_VARARGS);
}
#endif

void ILibDuktape_Polyfills_JS_Init(duk_context *ctx)
{
	// The following can be overriden by calling addModule() or by having a .js file in the module path

	// CRC32-STREAM, refer to /modules/crc32-stream.js for details
	duk_peval_string_noresult(ctx, "addCompressedModule('crc32-stream', Buffer.from('eJyNVNFu2jAUfY+Uf7jiBaiygNgbVTWxtNOiVVARuqpPk3FugrdgZ7bTFCH+fdchtKTdpPnF2Pfk3HOOrxhd+F6kyp0W+cbCZDwZQywtFhApXSrNrFDS93zvVnCUBlOoZIoa7AZhVjJOW1sJ4DtqQ2iYhGMYOECvLfWGl763UxVs2Q6kslAZJAZhIBMFAj5zLC0ICVxty0IwyRFqYTdNl5Yj9L3HlkGtLSMwI3hJp+wcBsw6tUBrY205HY3qug5ZozRUOh8VR5wZ3cbRzTy5+UBq3Rf3skBjQOPvSmiyud4BK0kMZ2uSWLAalAaWa6SaVU5srYUVMg/AqMzWTKPvpcJYLdaV7eR0kkZ+zwGUFJPQmyUQJz34PEviJPC9h3j1dXG/gofZcjmbr+KbBBZLiBbz63gVL+Z0+gKz+SN8i+fXASClRF3wudROPUkULkFMKa4EsdM+U0c5pkQuMsHJlMwrliPk6gm1JC9Qot4K417RkLjU9wqxFbYZAvPeETW5GLnwnpiGB4qjyerqFOKgT2aRbfvD8FS8dGjfyyrJHSdwqlsc0DxEy+jjhA99b398PUep0RKbxPqFfHAsurV//emWew2cwgvzgG8q+SuArKjMZtjFvvnULTeN4Q9eaY3SNT2eG1ERfCKdnNSdODvgIUyP5b9XL9/3aiQN3lYOQfecCcmKc0P/6eQf7K/Hw6lG8Z5bHp9ft86v4OVpKAWrKyS3GSsMtuDF+idyG6ZIcvFOKxoguxsQRQD9J1ZU2A9gDznacydDuiJIpaX7n+ikBYeOvgZCu7s6uMnZqrQqMKSBV9oa0rdvZ2ja7nAg6B/4IHJ3', 'base64'));");

	// http-digest. Refer to /modules/http-digest.js for a human readable version
	duk_peval_string_noresult(ctx, "addCompressedModule('http-digest', Buffer.from('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', 'base64'), '2021-09-29T19:36:45.000-07:00');");

	// Clipboard. Refer to /modules/clipboard.js for a human readable version
	duk_peval_string_noresult(ctx, "addCompressedModule('clipboard', Buffer.from('eJztPWlz2ziW31OV/4BWzTaphJZtOZ3JROOeUmzFo20fGUs5upKUlqYgizFFcknKktvt/e37HsADIMFDjjPTNRNWpS0RwLvwDrwHCL395PGjA8+/CezLeUS6O7t/IUM3og458ALfC8zI9tzHjx4/OrYt6oZ0SpbulAYkmlPS900L/sQtBnlHgxB6k25nh+jYoRU3tdq9x49uvCVZmDfE9SKyDClAsEMysx1K6NqifkRsl1jewnds07UoWdnRnGGJYXQeP/o1huBdRCZ0NqG7D99mYjdiRkgtgWceRf7L7e3VatUxGaUdL7jcdni/cPt4eDA4HQ22gFoc8dZ1aBiSgP7v0g6AzYsbYvpAjGVeAImOuSJeQMzLgEJb5CGxq8CObPfSIKE3i1ZmQB8/mtphFNgXy0iSU0Ia8Ct2AEmZLmn1R2Q4apFX/dFwZDx+9H44/vvZ2zF53z8/75+Oh4MROTsnB2enh8Px8OwUvr0m/dNfyS/D00ODUJASYKFrP0DqgUQbJUinIK4RpRL6mcfJCX1q2TPbAqbcy6V5Scmld00DF3ghPg0WdoizGAJx08ePHHthR0wJwiJHgOTJNgrv2gyIH3gwlJL9RIa6Fr/ScPp5p7578ybwAEt0M77xsfNOj7ccLIOAutHYXohvTz1X/IpjT7wpPae+Y1piy4g61EIyDxwKX/dJ9y/5llMvsmc30LS3m286B4ppGGFbAvBDf/LmfHjSP/8V3iYDDl5PxoMPY/nN29PhwdnhIGnYS5ldW47tj5n+7JPbO/Z+tnQZRuKCUK9pfzo9AJ3HyaNT4GzpUN01F7T9+NEt12IEdG06S4RxSaP/Hom9erk+a+ikafHbKLjhH2JIvGfcSwB1aEYyOHzsmc77/rzTzt4KkCRouktXhMHhr57s7uzstNudyBuBuruXersTglpGuka0dueLZ7u6NtZEfBK4lkG0FnmavHhKWlqrl5g1PnfZR+qEtJ7AVCjC6PiPZUbWXKftclmlg+8SGlDgv9m+qOtWOo1bYOLUXACjFvyNaDLBXqAnHMPYzsVyNqOoqe7ScYT3HshmakamZpBUWXSrSB7MELrQFA4HVD5ZUmfyin3oWJ4L/Osfrc/tooBqxVsJUmg0iBL+nSgO6k658khKHTL1AeCZxDKl0i7MkD5/pokjAopW3JouryY+BXgTDmHiejAFSyfSrWhtkE8tU2F4TOfQEJjGGQlDM/BivC0hhzen6GVVbfc+wb9WTJM9IzrQ1HGoewkB7Wey+3xvZ6c4m9vb5GRE3tnh0nTIKFpObY/MTXDCEPHWof0bhKDUE4sqCv54gXoYkyYImUtCs+bQ58lEi1lL7HArtUOtDU0adB0e2xcndOEFN5O+43gWmjMO0wFDQv9Tssu6G2THIKdvj4/5f4FnV7QwpMuOvXPybjXHWK/b5K8kA1jlWwJizZfuFYDB/uHygrOo2wbABkKeo4/JuRBk+in4I21BF5Z/Mwn1es75P5uzlWd4i9gxwy1s4hTB9xbvyr5nwtGYIPJE2UiS2FNtaqm/4R9iesH1JfS2JMgpqzhvh8uryPTpRBFPBmuu8y0t024kv04wqUYXmUpRz2A5VC9haXzM8ILR1oGFixdEIZoIdrgrBMhXzMYqYqJopIX42G7kLLiJiMYPb1Gtge7EjhOWEwF+RKlz4g6pBUsSXV+6oX3pwsIO4T1pM3lzKjN9Yd8FfTGIPIw8edL+UZyo9mdmRoIAU+GX+zi65thSIMrxbPJKuzWdINA4QeJGCIQ491i+bOC3m3pr8QWPLAptyPtyYcnSxJGTH38kOud5fx/jL/n9d5J8n5kQPNsP7+sbMfbtI0FOl7t/iMDw9N8uLEh+XvQ44NjzPqeWK06eNI0FH9QATPxPhlMeJphVL8P55NLxLsC2vYsvkHehSbd72AaOYQK5oh8bPbf1rV0QbmruLd4zXJn+JPL8uEuXv2WwxbFyrCvp9E24tEzHmSxoNPemHAuQSFgLJ/pfE0unduhjmnNOzSm4q6nsn+1pbJGJp8Mu3J2p0g6iw1xZsMLoQBIezbxggcmHtrLdva5WbbemhSFDTJuWIQ22QspLDsBrXAaAhLHP+uZtBrBzIJkjFhNUBV42DDn0zSCkQ4DNAXzc+dwZccTDad4275SmWJkMMRylfEFWFHoOfWtPdVUmxP9k8EWBI1yYkHwOiqLANpg6DDrKOZmaAUyLVtqOOncRTlkH9aw6trtcaxjmskzXsf0LzwymwBYrcShCHOjgMoC8VVbNToD6186pr5Jr7uVtZxqXi2waYhGFhMmMvUTB3PUaKCZnoVwx17Y788S5W3iuHXnBFr4HHsE7fRjCR5B2Xk1yFEISe82o/NB/O/772flw/OtLDr6zNpfgEwI7ujHI4XD05rifNqFxOuaNxMydWO3I1kdYZomLanpWHIAVkgEdvrQBM8u6JwGTFvy3l774wl586WVZdzxPEEAtFBq0swVLrnVhhhFL71PxjKzA9qMDz8UqLA3QaFmdQ89JI4+HQ+qIdcJI3UVZApnWlDZiuAI7UbDM+w+pJ4pJL8wp60LX4HwlQy3hBknFznK1BqJzObWoqD+oSK73Y3nyv+jaIAg8VA9zipUJwTbLPRo+U+rQiMoAOUsNmB6sqbWMaLzkbLHqr4luu1JNemoXwn1CJ5pTN9Vp/bp9yyF2QlYYavfuMgHrtH0b+9MORfbhRS8x/XjmgPRWjnrmkOLgGEvkTirJJlHyPZgp1VEBDdIsWOaqb3KV7l8ZLeVg+fP3UPlHDZW5SMm073ug3DBQ1qxNceq44+V+rCagPEzky4HrpI4SX1b0Kw8sIDZklLtu7iRjfqBF8utiIwR+2Z+rECqCLsBMfK3jXWK4rAZz39gQE6ugAnDeRsHNrcqqkJ67ZAfntmKiWBiBMHFX45Bv83EkH0gEHyeyzeAzo+3JGlnnKxrpY7LXEyOL7AX1llG9M7dwM3TMeysh9IpjFN3kTSqFJMrHhTRK8GdaFVJnVk889koAFtZjik4lVBoEK0I7BiOwrXIdhYUAzM6EuesJLk4iuo7YuiG3DqjYZt04wIl7kUS1GYmrL7bgo/WrmozIe3hsRMTcmbS5iS8msSbDOApG/tp2qF4e6QzyUVs7+AmLTsB+vNWO3yz2ytM+G+BdljYLUQaBmJB4eh4FB+613s68TUqZMoXIWsNoCqtC+BPwvdtiU8kWa5I+4UgszQm14J6KDIAGel2GCJseCJEyFqhLMzHcVAI/IGUNMyemXzkAHaBrkaXsgq7iU787LOVaCexYamVbwin7om7LFgorsbyFyub5DfLlEq/Qa0AuDv3nEarySCUeoOh9dLpWHDbI3A+0buCAPuzuVrifCTQL2ys/wNcK1BoCS89n4Wmmg8TpkBPTtf2lw7ZM8uVRdarAtnNKl+mlSl2tBBVikUSTiOfopEo6l4tc+o2FcnTXb4pHrYqOnfn1fAKfg9CZ8ND5pqImkx8iJepY4XOrSx3QYaNaLcplNYcMHKiBGe98eI9f3thr6iAszEXjRMAg8ffQguQMU9POO9OR1l/Jw41FYrZzcDx8MzxMsOChx8DtR96igOXoJF7nvzMDG49z6RqOfXXWPz/U2gYpbDeVYXx9Mr4vwrfj1y8mo/H58PRoI5Rvzs/uzeSH0eB4ctgf9zfCODw9OL8vRhy7EbLzs7Px++Fpgu3c86L3tjv1VrWq0nTK+r8MBASc4BEesqQliCrIZJujOykt7MitQX5i75jKx3/aSh1mBIxuXKuAUikuTq/nXkNOmh50bEQtNwx1G1NhdRNXtZJhXIyGeMyzlOhyHpua9iENWQboBYPrXOqZa8LVrDmdZm/1RG5ufGp0ubigQY4c5mhw8YpBAFKcl6zWXEyMG9JXdMOKQfcFnVRN9onMwz3h4Vo05lpajs6m7SJAhXvHh60MBkBR0QXs7nSfqaSIT3xQgE3QG8i3ccHMSRanRUFGBSn4MICnEMoZh0z7JLAGEFtGEz6QnX8YgKACOtPBkp/hcQx+RkPn5e234AX3uscDyGYg+c8dSC6ht4bmRIws00zF+Maz0d8qM+X8yAs7Cu87NvztviMj03buO5afBCobXT2ezfERjaNDcgY9P8+VrqvK58G0P//ppz3uynOH3A2W3aK4DRCcwURgxMzUE67CypIpDiBWu3aHJ5B1YmRieI0nDeThjcaBK4gC7yaOe81F15DJgC68ayp44pkySovPBWjBVUWfO3WT4nXBdYvbGgaRtpty+UVt5sfzBfyxih6to2+e/n3b6tS/T3EKP7BjbYz8uRnOD/AcJz9moypafdNyVXmpSCy/W9L5xIqi1EOBm2AJ2V3iscMUTvJO94uqwLejGk2Ptn1hu9vhXMPZgD/STwSsebG8lr37qrpaBv7r5c7MqWzXKdkyLM+PORm221mxLWfND8mWufaIDya65eHP4RamOyUihKbVNxVoikAT2ItSuPmhv5PLgPqklVmMX7CXFvmdHZZrgcch2qdPrka0/9Hgpbm6Iluv8bPWKs5AhuRWq2qFVyBW3d7f7dl/PX3de/rUbtcNqIWIDz9m9yfbiLwr6oZGC3/A0WQgHtLb52dPcTjhJQ6dg/m4+7n9tNsMEJ4g4GDYAepdY7e9v49kgArWI9lDbWyCxppfidBiXM+RxiYkJsP3Yy1o1YqfNJ0CeHywrGhGWv8VtmAlkXDXSHy1zFd2aN1pn1wsrX9y89q5Mu1okN92QluXXVNNgT1xDFe24+jpsQoJRBuCy2h49Mvw+FgiVbDIeFc3+0HhR8kCPyfh9b47BwJguWov4CjbzKsolvq5IAJ9Rf4M0s125hKcYo/iDkK6kFAGvnRaYZlVzTA/5OVOqwCi7P6vwZZLE+HV7MCkAiqTxIbbI7jqxPUmq08X1pwPUJSP5gGkAH+IinwiCMVKu8SaNqy8f+PjlxOgFCs+65rC+/dqf0m1Pw/yP7bW/59XDKdZZfts5RbKtIbwK/6HqEvXotu4mv5QZfEms/e9KP69KN6MFHy+tigermx2lqlpZfz+ZXDLBO2Q7/94WT2CjYqzewyhu7o28hb0wpvesLUJ8VZuWHGWXvXUlSKLlMaXjnwFrSsTbJUsQ7yQBg8g4t0sC8KqGfU1V3xQ5+i1UufgTVxdH+EvUvf3yQvyN/LnLnlJ9p43EQjbJwgmAefSCzgWBcxUQ56BirxoA4L0TZcpTXNkacGuAbIXBWTPNkPmxzX+elzPn+Vx7XU3wxWZwSWN6jH99LzA1YsNMfELeGrw/Llb4Oj5ZngyD1yDqlsQ3m4ivHpk9LrM+7BEMXU/+T25hnzQ6wb6ndKwlwgtfbO7idAAWb1+p6BTY0rfbGRMgKxG5zJMLwqYNrEkxFSlcynU1IokgW6Cp9ZiU8ipHWU8bWJHGDow91Z6VuWOHqarjTRadKnp5p/PWUmVm2XAonIKmt/u5ePJ+eAfbwej8dn5S1bCbYQhu3BgTtda7f5hTHmqv7WUZz2rKB8NjgcHeC1aSnk9hvtRzo2hluy4WxXN4/750WCcElwD+J7UgkHV04qdKikdngwyOqtAKqispxPLBUUBwGIVrbE0pa5YzYpPzZoxeXIcY+ZNksvgZl7AuU+sueEeuxp2FvJ4mqS0sqQlcVSFhqKwmlLDM7i56V7S9PSDQFMMUU2XkiYFPeAzjfwdfUYqvuxTZ8JuOdkiuxtRz5IQFc3KConqUYm2zDzSjpKJ1OMpOXMgPvLGXdlzTxV2r1xIX8hrL1iYUaLMjVIYRprAeF4y8pIJr2ZsAvWuiS+I6yvulGWbGyjmLqsX0euHSNEe/LSI46XOovAbo1X5TwKSi2CywnN6A2GxdD05oi4NbOvEDMK56Uj3W+HRjr2uuAg5ZXc2gYmub/jJj71uZ+rIo65o4FKnYlzSQRqZvuQDTvgVKNoRu/GF3SLUrOuxZ1016/nWdZK+vHfMj9z3wPFCelBI5ZV9h2HakZtP/9q0HVy0VY87olE68DDJvkt7n/nUlQmSOkvNulDfw3gZ9yknVJcvJOXx9Id9orhyEE9pxPDyDOSh5HZA53moOcipIqd3Q6YTmU2yPs8bUdw9jg77ZGs314EbQ9LtvT2lXYzYuV45XFxNZGwNfkvOfrj6LfZ8/PjXLImjzrbMJNWJ+WVbW77gRfLKLqm3cBDym9DvJ0cPCz978oubkKvKTcijk8HJ5OTs3aD/6hirnjvrnZ2druCG5Lt2v7u/Cvf3IM5vsPCjm2/sKIuur7TrSOVTs1laRGXZNS40b0H5pjTbgCBCcC16IzY9uqSQBkPAPVFC5rxjLiOPHZ7lN/sJStPQ0yWuqwiItzOkQpGKrUWTQWkhS++SJ7hfnFyVxy7fk5apOfUpOsK6iCO0ynqh51rz05SLHAaZ52e56LDEm8oWpuWFmdtgRyuo6Duycw/JXjgNQ/OSbl14a/Y7/4yebLQai2L5VQP9UoTeTl0e3+soHALMwLLCf3zFgFDvl+/8AOURXWavrB/bOIk7JywIncVVLsfLLxuowutsgtdR4z1jl/p1pnRmu1mKKYMw4kOELUMO2YpU5xI3EAtHb2oG4ZP+CpqfBmmzDUk+q+LrXtlyP71Q5esPsYpPyU/GH+7n4qUIs8Nuqzm4EDvkx73is5naLT99R/7UxZOEytNwRbDKw3H5WcizHP+WHBeOrdbmO5JqBUPJgFpNYr0CWbF7SV8WBJ5gLxVYaaKaqE8lP+RvpRhf8ruhFGhzKiiRVrTj5DyxYMnSoakZ10w7jEJWKtG2l2Gwjfe3OkxLmYi0dvmaXekXsjNVvareiXco/jI/N2wzP5HNp5obWWbNryYIvBXB9VGy9h4tfcSM/1OOV6PDxPBjUyk7llmcovgWqSpfK0e4Wm8rhyqFfgA9uZG5i5kxCZDf9MqH5G8PFwfn28rA8Etq05H8a7GzdN0a9Ja+l3c/53IRv/b+H44Bp8U=', 'base64'), '2022-01-03T19:17:33.000-08:00');");

	// Promise: This is very important, as it is used everywhere. Refer to /modules/promise.js to see a human readable version of promise.js
	duk_peval_string_noresult(ctx, "addCompressedModule('promise', Buffer.from('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', 'base64'), '2021-08-23T14:25:14.000-07:00');");

	// util-agentlog, used to parse agent error logs. Refer to modules/util-agentlog.js
	duk_peval_string_noresult(ctx, "addCompressedModule('util-agentlog', Buffer.from('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', 'base64'));");

	// util-pathHelper, used to settings/config by the agent. Refer to /modules/util-pathHelper for details.
	duk_peval_string_noresult(ctx, "addCompressedModule('util-pathHelper', Buffer.from('eJy1VFFP2zAQfo+U/3DrA0lZSEu3J1A1dYVp0VA70bIKiRc3uaQeqe3ZDqFC++87N8kAMWli0vIQK77Pd9/33TmDQ9+bSrXTvNhYGA1HQ0iExRKmUiupmeVS+J7vXfAUhcEMKpGhBrtBmCiW0tJGIviG2hAaRvEQQgfotaFe/9T3drKCLduBkBYqg5SBG8h5iYD3KSoLXEAqt6rkTKQINbebfZU2R+x7120GubaMwIzgir7ypzBg1rEFejbWqpPBoK7rmO2ZxlIXg7LBmcFFMj2fLc6PiK07cSVKNAY0/qi4JpnrHTBFZFK2Joolq0FqYIVGilnpyNaaWy6KCIzMbc00+l7GjdV8XdlnPnXUSO9TADnFBPQmC0gWPfg4WSSLyPdWyfLz/GoJq8nl5WS2TM4XML+E6Xx2liyT+Yy+PsFkdg1fktlZBEguURW8V9qxJ4rcOYgZ2bVAfFY+lw0dozDlOU9JlCgqViAU8g61IC2gUG+5cV00RC7zvZJvud0PgXmpiIocDpx5eSVSh6H23OJXZjehotdxBG4ZRa7f09VZ3/cemtbwHMJmD96MQVRlCQcHjygHaZHuuWNUVd5SRRiD0jIlqXFaZ2E/NiTWht2eKpkllVsYjyGouXg3CuADBDc3AZxAMAjcGHZJm4Sxkir803ZlWg2vLdFkfJpyn4aYt6m/Sy5ezfhnN9TOub2psbFMW7OiAQiDOOi/tK2DHsdWXsga9ZQZJM9QZN0xvEc6CQ+/ObaSq7UbU1GEw6jdKlEUdCGP4H3/1LF5Lu5tc3L0yNYtWBr8SzP/xeHXN3H0H9rhFo220qJ12cX2bdrKrCqRvKXfp3Uqu0tx6qK+9wtCYKEt', 'base64'));");

	// util-service-check, utility for correcting errors with meshServiceName initialization. Refer to modules/util-service-check.js
	duk_peval_string_noresult(ctx, "addCompressedModule('util-service-check', Buffer.from('eJy1Vttu2zgQfRegfxj4xVI3kdLsW4Mu4HVdVEhi70bOBkFdLGh5JBOVSS1JxTaK/vsOZamRb7kA7bxIFOdyzsxwxPCN6/RlsVY8mxs4Pzs/g0gYzKEvVSEVM1wK13GdK56g0DiDUsxQgZkj9AqW0KPeOYF/UGnShvPgDDyr0Km3Ov6F66xlCQu2BiENlBrJA9eQ8hwBVwkWBriARC6KnDORICy5mVdRah+B69zXHuTUMFJmpF7QKm2rATMWLZDMjSneheFyuQxYhTSQKgvzjZ4Or6L+YBgPTgmttbgVOWoNCv8ruSKa0zWwgsAkbEoQc7YEqYBlCmnPSAt2qbjhIjsBLVOzZApdZ8a1UXxamq08NdCIb1uBMsUEdHoxRHEH/uzFUXziOnfR+NPodgx3vZub3nAcDWIY3UB/NPwQjaPRkFYfoTe8h8to+OEEkLJEUXBVKIueIHKbQZxRumLErfCp3MDRBSY85QmRElnJMoRMPqASxAUKVAuubRU1gZu5Ts4X3FRNoPcZUZA3oU2e66SlSKwW1U38q1E9kEZ/jslXz3edb5uCPDAF+uLxXWEG75uMe12yPKVPNkPrrt/SK5iZt5YPLC9RV5ZZ8HeJan2Ja88uPl0O7oMrmbD8murNBfVkNx59HFMuB5PJqEABsSxVgj/c8xS8jb9Al9OvuNb+ZqOGbMXmzbOBua36Qe0dCytEYvvDzr4V3aZfJ+10wQTVRHX9oH4LMjTxZnMH62f+pSHSFp56OuD6Gj3f3989gKPCEiS51GRywKEVhaZUYjdbRxB83/+EucafAmbH984yYSaZg7fyn0x+y6h+/d5MjTCEW1GdeTrlKRczmjOLyikVv91bYdNZYbuxmjZ9VYvex+PB9WTSL5VCYfpSGCXzGM1kUtddv7Zhf02/2oP4E0lNJl34DfYaioyjBTX+XxStu9sBh8rtrZ4pd6VJALgo8Wl/NUP7CDQNUuN1A1zhHgw6YpVOjiIz8z/ePh+/9lyZfT77QsQPe9543+gF2jBl9B0Nea/b6fo+fNtGWE7t70Rk3lv/4tChq2HCezJQklKubczEZvblk2GvM57Rt/KLJluL1xMT7gX4KozPTLxGXjX5GjlQjJfGPWK6aXXEI4SPkD3g7PgAfZyF9tHwFmWeW6zViNR0LyMYTTcVOTM0bhaPP/mE0VXD/st/P+++e3S9kLMyR+o+ulMa2x2794RWMqYKWbOeYcrK3Dzl6cfVw7PnYxt1m13jtuLxPwpX1LY=', 'base64'));");

	// descriptor helper methods, see modules/util-descriptors for details
	duk_peval_string_noresult(ctx, "addCompressedModule('util-descriptors', Buffer.from('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', 'base64'));");

	// DNS helper util. See modules/util-dns for a human readable version
	duk_peval_string_noresult(ctx, "addCompressedModule('util-dns', Buffer.from('eJzdV2uT2jYU/c4M/+HWbWN747V3SSZNlzIZso+UZsNmAkkmAyQVtgyaNZIrycuSLfntvQKzmH0AmWYmnfoDNtJ9nHuudGwFO+XSoUgnkg2GGip7lX1ocE0TOBQyFZJoJni5VC6dspByRSPIeEQl6CGFekpCvOUzHryjUqE1VPw9cIyBlU9ZbrVcmogMRmQCXGjIFMUITEHMEgr0MqSpBsYhFKM0YYSHFMZMD2dZ8hh+ufQhjyD6mqAxQfMU/8VFMyDaoAW8hlqnB0EwHo99MkPqCzkIkrmdCk4bh8fN1vEuojUeb3lClQJJ/8qYxDL7EyApgglJHyEmZAxCAhlIinNaGLBjyTTjAw+UiPWYSFouRUxpyfqZXuFpAQ3rLRogU4SDVW9Bo2XB83qr0fLKpfeN9u9nb9vwvv7mTb3Zbhy34OwNHJ41jxrtxlkT/51AvfkBXjaaRx5QZAmz0MtUGvQIkRkGaYR0tShdSR+LORyV0pDFLMSi+CAjAwoDcUElx1ogpXLElOmiQnBRuZSwEdOzRaBuV4RJdgJDXpzx0Nhg13gkxupTxJXjlktX805cEInEaqhBp1ddDrEUR3LCHfvTC8qpZOErItWQJLbrH0pKNG1i9gv6WorLiWM30mGSkpT5UYIWeSyW5qavqB6KyLFfUN2keizk+WsiyUjNLJd5I6LJNpnfEclM9539vcpjtwA8ofyr/K+d0dHX4nkWx1Q6rm9WEH2L2+1R5fT4OkteVAwOFnazFMeA90wg139HEqjVYM+de+RkX1fJ1SmuNgRqXPwjKmnsrMH8WjDc97LFPlMT9Sk8g8ovFTiAypOnHjx+uoRmrkgsnwuJzYWd9tNMDZ0cwVel9mD/iVvkSIsW7hg+cNwFieaawnhopMNxCmWupNvz1jWokNHNHdylIxY7Z/cHw26edjq/YXWZ5IDlaDMzLSz+hPHs8o6lHyLUqLhgZgOfUilC3LSIhl7S8ATLceygz3ighrYHHRtvvUXymYevdCQyjTeJ0Wy7ujosuGObVqPzNSQndOFqJrUzr4c1CAuMVmF6KwGV8u5IJtBtc8bnq9ixQqIhoDoMUIlEcuGHgsfwN6BipsDJiCoqUWVwBHHYXW6D/aeN/8j4HHZPzLNt3R/dvrLXTOLo0fFpzbKqG6xSrFvHYHU2WqJYOqy2X2W/NU+qDx8yd4P9JnyzHe38xL4EH38MDJVID749Moo9sN0Nnp9rCkVdo7vX9ixAnrbK9rlWq8CDB9Du7PdqNWvZBGtTNdsVVCD0Z9XtdvNfy8NmeO1OpbcFzEXrvI0dMVtwg8mm+QXa3tpk1nTtSuxyesk0LuBVmzFh+hgnnMWwlpNbujwXD+eP1lnTT1GMqHNzY/u4NUeodTdUBzdXOHTobaVfyFGnV/CYrryTRyQUd72R/2eydG87VZhplsDuLlJwvyB9Z0W6+m6KBF8g+Njtqh3Y6Xb1zpKZbrfT2dv9tdcLvplc1AtCdvZyWym7oTPznwNYPFme4doDjNh51Jvf9/+z2jPdSnv+lfrcrz9m2+vZgaYGW4hQ9ZZyzZz9czpRzjeQKIWHvHAITi45fpoQjct8tNSpkODBxZ59WNkHxaEYD2J9FS0GzTUSUYbg8CwkpFZY4fX3WKGOPn6Zn1eLkfDI8qiyNk7hULM2UkQkmq4Nda3F9wSKaEyyRK8LsSp9RXJhekdUJPof7kamjA==', 'base64'));"); 
#ifdef WIN32
	// Adding win-registry, since it is very useful for windows... Refer to /modules/win-registry.js to see a human readable version
	duk_peval_string_noresult(ctx, "addCompressedModule('win-registry', Buffer.from('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', 'base64'), '2021-12-03T16:34:49.000-08:00');");

	// Adding PE_Parser, since it is very userful for windows.. Refer to /modules/PE_Parser.js to see a human readable version
	duk_peval_string_noresult(ctx, "addCompressedModule('PE_Parser', Buffer.from('eJytV0tz2kgQvlPFf+j1RSjL8rJCEVM+EOxUqPWCC4FTOQ7SCKYsZrSjkTGbyn/fHkkIiad2KzogNNPzdffXL6n5oVoZimAr2XKloNNq92DEFfVhKGQgJFFM8GqlWnliDuUhdSHiLpWgVhQGAXHwlu7U4YXKEKWh02hBTQvcpFs3Zr9a2YoI1mQLXCiIQooILASP+RTou0MDBYyDI9aBzwh3KGyYWsVaUoxGtfI9RRALRVCYoHiAT15eDIjS1gJeK6WCu2Zzs9k0SGxpQ8hl00/kwubTaPg4th//QGv1iTn3aRiCpH9HTKKbiy2QAI1xyAJN9MkGhASylBT3lNDGbiRTjC/rEApPbYik1YrLQiXZIlIFnnamob95AWSKcLgZ2DCyb+DzwB7Z9Wrl22j2dTKfwbfBdDoYz0aPNkymMJyMH0az0WSMT19gMP4Of47GD3WgyBJqoe+B1NajiUwzSF2ky6a0oN4TiTlhQB3mMQed4suILCksxRuVHH2BgMo1C3UUQzTOrVZ8tmYqToLw2CNU8qGpyWs2YUpVJDnygmrW8QEgCxGpJM70nTqR0lRWK17EnXg/IDKkNdx6JmplVis/kri9EYlhUC/Eh3v48bO/X/VCXEkjVDO80DDzmy5uemFDBJTbW+7sgOtgyEVBcrFVNJxS4ubWXBF+xRX07x4+R55HZYP4vnBqXSt/lKvTUp2ClAhSsf4uF2OCiBuT9zCxYRVvJ3uZOYn9Ev/F9ntufW9WHVp16Fp42yliHtSy7fjUHAu33X16rLXMhhI2Jhpf1tpd/TAPMLJDgnSb8Ns9GB8H1oNhJkAp7fpSKyk2UDMiLqkjlpz9oyuBcSK3kMQ1I/LnKdfGs9Ke7ZiMHds9NHzKlzpkx47ddtCxbsvMu58dC3VV1xDIynlurOi7kbrbsj628Lrgsm5LBJ4f45aU91LfQuxGziqnsEC3VaT7WIeDxIPRthzjTtN120FO1X5bX0nCNxKSkS7jvdc1+kWZBWp97R/C9rpdK8HtWmVwUfoKrks9EvkqxpzzVy42/DKobgwe49S9AJzlS3oYawR7APETQm3MNAQ6zW8ny/mzZweuGzfA+wuZA7/jeDtZkTu8QvJmRXxY62eN3Fl5Ke0z1Djvs6cSia/NL/aiaD19GRzQX+BpuKLOqx2tn8V1Ziz86VpFnjWzE28oXB2bva2F45Z56syI42wkvu4fD0SR88d7J4/PeWmAdsfM3N6V6bHo9bZ4tmpbn/NVm3bDYp5nkThj46eO2T9ZQUMqlR7FRNGZHo77ND7rba8kVFpR53Buy5qkccrmD6KeBpVvxFZEqrJlqsE+dUu1v84uPkn3+1/xabfKsno9QJb1awJk/ZdAlw6Q1ft1AWq3D6N9bpAUpXYTNx0tgPoj/XaKbSx+R53suvFfZMmcOzBQ15mea12s6MvTqOj9ENXrUZakStZR9FvG5VQ47hv5yUIi/OWKObqHOnsEbP3cx28Xnqxiy9KP+oTHZKgAz8itucfU/X7lHo2iQo4cjhqUj4cM3rPxcsWXHFoqmbP5ULnGLeRGy7xkzjFgbN3xckljMSv2nl21/ISa/YviAvMFX4sugzxsnmJVCHbC82Jeoe+OL0KaOp+bcfobKc0ovRq/Fa2FG/m0gR9xQipde/GHUZyD1cq/OIedMw==', 'base64'));");
	duk_peval_string_noresult(ctx, "addCompressedModule('win-authenticode-opus', Buffer.from('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', 'base64'), '2022-01-21T09:58:55.000-08:00');");

	// Windows Message Pump, refer to modules/win-message-pump.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-userconsent', Buffer.from('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', 'base64'), '2022-01-19T20:11:35.000-08:00');");
	duk_peval_string_noresult(ctx, "addCompressedModule('win-message-pump', Buffer.from('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', 'base64'), '2022-01-24T15:21:02.000-08:00');");
	duk_peval_string_noresult(ctx, "addCompressedModule('win-console', Buffer.from('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', 'base64'));");
	char *_windialog = ILibMemory_Allocate(34645, 0, NULL, NULL);
	memcpy_s(_windialog + 0, 34644, "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", 16000);
	memcpy_s(_windialog + 16000, 18644, "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", 16000);
	memcpy_s(_windialog + 32000, 2644, "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", 2644);
	ILibDuktape_AddCompressedModuleEx(ctx, "win-dialog", _windialog, "2022-01-25T15:47:41.000-08:00");
	free(_windialog);


	// Windows Cert Store, refer to modules/win-certstore.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-certstore', Buffer.from('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', 'base64'));");

	// win-bcd is used to configure booting in Safe-Mode. refer to modules/win-bcd.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-bcd', Buffer.from('eJzVV21v2zYQ/m7A/+HmD5XcqkqXFQPmIBi8xG2NJHYXpQ2COAho6SyzkUmXovyywP99R73Y8lvbtWix8oNFkce755473dEHT6uVEzmeKx4ONRy++PUPaAuNEZxINZaKaS5FtVKtnHMfRYwBJCJABXqI0Bwznx75jgPvUcUkDYfuC7CNQC3fqtWPqpW5TGDE5iCkhiRG0sBjGPAIAWc+jjVwAb4cjSPOhI8w5XqYWsl1uNXKTa5B9jUjYUbiY3oblMWAaYMWaAy1HjcODqbTqctSpK5U4UGUycUH5+2TVsdrPSe05sQ7EWEcg8KPCVfkZn8ObExgfNYniBGbglTAQoW0p6UBO1VccxE6EMuBnjKF1UrAY614P9FrPBXQyN+yADHFBNSaHrS9GvzV9NqeU61ct6/edN9dwXXz8rLZuWq3POhewkm3c9q+anc79PYKmp0bOGt3Th1AYoms4GysDHqCyA2DGBBdHuKa+YHM4MRj9PmA++SUCBMWIoRygkqQLzBGNeKxiWJM4IJqJeIjrtMkiLc9IiNPDwx5g0T4RgZC1Gc4j+16tfKYRWHCFJGq4RgeF0fZkj/kUUALOde2lS7cj5X0yQmr7uIM/VeUGXa+5KKY3FpTLgKurDt4BrVez5vHGke/HfZ6fT/AgGtzqubArZW/Ww5YByiSkZk8+olSKPTCuquXUbixDmSi6aEIkGUdrS9LYVsB04xULF20/To8ptmbnnp2DL6rpUdhFaFdP4LFlgFUarcmo2hDfMq4bs24ts3yisGIC4wJ4SZol8yO7LobU9C1bfVUT1iFwvSMGw/5wGjbeC2Um6SwjQVuUjqVqWc7efwKBFo+UMgJQipzy+8Km7A0WIg+4JzksgMlg2WRCYsSXAl9kFykmnJ/StKUO7ek8I6E00P51iJ70G6iiEl6mkOLrVS06ewqGQvxNHrLXE31bx6Pl8edzPB6Sv/AHCYk6ynswArVz5TOZXoDjFDjVoB+MLUZipTIHRT/ZNyW2EVhupbHBnghA/RQTahg23H27LARlj+JnGXi77nC0DSoOZF8Td0tjc8+gTdnrRv3XPosuqDmSkWBCPRuvKvWRa93kvF4IoVWMvJQ01I2p8gQqr6UtNRBPZXqodezKGYlcA6IJIqMumzN2kweHn+Ra1nrCcuptO7D0Uouq1mPlJcDlkS6AZaQAi0oehbJ024mRQrcvxNU84ye8DtSYZIEfKb9IdgfHzDNgvVSRpDcHDQcH+/njG4eX5wT5OxWD9iXBqfLD/nHJ0p9vRmkPNmz9f612Pw26KKkmdI2ffvs+1aeeJhQbZiKZekpFtLao9JfU4dSJL8cm6T/M52XikTDevG/urKkmny6DcoI3UiGdllT/eizJYoPYJUo969RoOL+BVPxkEVE8Fu6CGhUHv8HTTa/hCdPVtGQJgRMUYzraarPfn9prQI4kkFCmOgqLJWmi8pWBu8sio3dy87q2O4Pp7Fn3Snyq1FMnO1y1dheypM1owmjGD/j14ZvZuT3mUYxcfI7TCN/Oqum21hNna/l5Wu4CXNAYQ7oW7gq+Mreuv0P6GtTCanAvFWS/sjoub3OnQM1U0+MpprzKRob5ca7vrshvFYtPyNnRnr33d+Qym3lexXT7tg4ZopPNnuf3n7KV+7yiOl/uGk+k/ru/T1+muEz+tN52NgvYEbRxiyv+ap1f9E9bd13WlfX3cuzvZCK0VfIHj4hU7Ty/wrgmwwvdi8XVia7dO84k7f82Q7W9zC+9KPTvbxonu90YsNQ6TWt24t/ATXtbrU=', 'base64'));");

	// win-dispatcher a helper to run JavaScript as a particular user. Refer to modules/win-dispatcher.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-dispatcher', Buffer.from('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', 'base64'));");

	// win-firewall is a helper to Modify Windows Firewall Filters. Refer to modules/win-firewall.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-firewall', Buffer.from('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', 'base64'), '2021-11-09T11:49:08.000-08:00');");

	// win-systray is a helper to add a system tray icon with context menu. Refer to modules/win-systray.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-systray', Buffer.from('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', 'base64'));"); 

	// win-com is a helper to add COM support. Refer to modules/win-com.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-com', Buffer.from('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', 'base64'), '2021-11-09T15:16:19.000-08:00');");

	// win-wmi is a helper to add wmi support using win-com. Refer to modules/win-wmi.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-wmi', Buffer.from('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', 'base64'), '2021-11-09T11:50:29.000-08:00');");

	// Simple COM BASED Task Scheduler for Windows. Refer to modules/win-tasks.js
	duk_peval_string_noresult(ctx, "addCompressedModule('win-tasks', Buffer.from('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', 'base64'), '2021-12-03T17:34:14.000-08:00');");

#endif

#ifdef _FREEBSD
	// Helper to locate installed libraries. Currently only supports FreeBSD
	duk_peval_string_noresult(ctx, "addCompressedModule('lib-finder', Buffer.from('eJytVVFv2zYQfrYA/YebUIBSq8px3pbAA7w0xYwGDhCnCwrbGGiJtonIpEZSdoIk/30fJc1w0m4vmx9M83j33Xd3H+n++zC40NWjkeuNo9OTwc80Vk6UdKFNpQ13UqswCIMrmQtlRUG1KoQhtxE0qniOpTtJ6XdhLLzpNDuh2DtE3VGUnIfBo65pyx9JaUe1FUCQllayFCQeclE5kopyva1KyVUuaC/dpsnSYWRh8K1D0EvH4czhXmG3OnYj7jxbwmfjXHXW7+/3+4w3TDNt1v2y9bP9q/HF5WR6+RFsfcRXVQpryYg/a2lQ5vKReAUyOV+CYsn3pA3xtRE4c9qT3RvppFqnZPXK7bkRYVBI64xc1u5Vn/6mhnqPHdAprigaTWk8jejX0XQ8TcPgbnz72/XXW7ob3dyMJrfjyyld39DF9eTT+HZ8PcHuM40m3+jLePIpJYEuIYt4qIxnD4rSd1AUaNdUiFfpV7qlYyuRy5XMUZRa13wtaK13wijUQpUwW2n9FC3IFWFQyq10jQjs9xUhyfu+b96qVrn3wTxVESu+FUkYPIVBz2KM+SaujM7BL6tK7kBji9OeP+7lHMTYCl1d2oKdeVNvxw3G4GhIs8X5wZJvZFnA1g0oZo3hjw6ZJZl4EPlnyClm/aVUfbthKc0YlkXSojQBmXWFrh0WAzDGfnCkVcwK7jjiD3XFeUJPjWSbyA9DyjOnp5ilWsfJOb28zSFV5vUh4qi6X0MtK00RfSDfGiwRPRNg2HyumP9+ZjDw/T0xZOFDixG6+N1JSi6leTTdcK/IK7m0hHp3shDF2RyXiuQq5sPhqefWxrjZ6SKlUi59XOtTgaPzptlgAaL0wg7Jn1lHoUv+5AG75IMUrbYeJrMaQL8MfJYW7N2gBZor8SDdXEWvqt9z6S5hj5Pzo3latPvtBDL0bxsnWZsScKwLgUZiHyb9PUNs0lgbxfR64PgTbDO5AAY3zt7hEsRdZxnYssRTzbXC9awFqLZxHnAHFk/e9YxaiJc2Ye+/y+vf9PX/CewHCmMHhX0sIaCDyPxY8V5VjW2XHVn9sOfsSGkYttP3KUX9TlXYzfhiOIxex0bfaWB+EAF7zfCtCnq7rNR585L8sxI6V9z+rKrtJt51lnaCMNdGxVha89IIfu9/4fjFP0NbXdSlwKjwx+W84PxzBIe/ALtaHGg=', 'base64'));"); 
#endif

	// monitor-info: Refer to modules/monitor-info.js
	duk_peval_string_noresult(ctx, "addCompressedModule('monitor-info', Buffer.from('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', 'base64'), '2022-01-17T13:07:44.000-08:00');");

	// service-host. Refer to modules/service-host.js
	duk_peval_string_noresult(ctx, "addCompressedModule('service-host', Buffer.from('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', 'base64'));");


	// power-monitor, refer to modules/power-monitor.js for details
	duk_peval_string_noresult(ctx, "addCompressedModule('power-monitor', Buffer.from('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', 'base64'));");

	// service-manager, which on linux has a dependency on user-sessions and process-manager. Refer to /modules folder for human readable versions.
	duk_peval_string_noresult(ctx, "addCompressedModule('process-manager', Buffer.from('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', 'base64'));");

#if defined(_POSIX) && !defined(__APPLE__) && !defined(_FREEBSD)
	duk_peval_string_noresult(ctx, "addCompressedModule('linux-dbus', Buffer.from('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', 'base64'));");
	duk_peval_string_noresult(ctx, "addCompressedModule('linux-gnome-helpers', Buffer.from('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', 'base64'));");
	duk_peval_string_noresult(ctx, "addCompressedModule('linux-cpuflags', Buffer.from('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', 'base64'));");
	duk_peval_string_noresult(ctx, "addCompressedModule('linux-acpi', Buffer.from('eJx9VVFvm0gQfkfiP8z5Bago5Ny3WHlwHJ8OXWWfQnJV1VbVGga8F7zL7S6xLSv//WYBO7h1ui8G9ttvvvlmZh2/c52ZrPeKl2sD46vxFSTCYAUzqWqpmOFSuI7rfOQZCo05NCJHBWaNMK1ZRj/9Tgj/oNKEhnF0Bb4FjPqtUTBxnb1sYMP2IKSBRiMxcA0FrxBwl2FtgAvI5KauOBMZwpabdRul54hc53PPIFeGEZgRvKa3YggDZqxaoLU2pr6O4+12G7FWaSRVGVcdTscfk9l8kc7fk1p74lFUqDUo/K/hitJc7YHVJCZjK5JYsS1IBaxUSHtGWrFbxQ0XZQhaFmbLFLpOzrVRfNWYM5+O0ijfIYCcYgJG0xSSdAS30zRJQ9f5lDz8uXx8gE/T+/vp4iGZp7C8h9lycZc8JMsFvf0B08Vn+CtZ3IWA5BJFwV2trHqSyK2DmJNdKeJZ+EJ2cnSNGS94RkmJsmElQimfUQnKBWpUG65tFTWJy12n4htu2ibQP2dEQd7F1jzXKRqRWRRUXDS77yyruR+4zqErha119H25+hczk9zBDXgt7L2FeZMO0zvve/iMwmgviOb2YU7xDaooY1XlW54QjGow6A7ZFWUKmcEW7XstZdBzdhGjHAsu8G8lKT2z71lGuqmpwakSoxAO8MyqBq9fVRRWAe6oXjrdi8z34memYtWI2EbIIy2zJzReAC/HYLxomaMTb6/x8Cq18yGjAglDLpyCCcvU5zGTQmDrpX+Ampn1NbwRO4QNGpYzw67PDIWXEE718AdODZSc1FAYz1J4wzPZuhFPwTn6h8N2kSpYVSRGTy5vNqumKKhnbkA0VfUGyMgnaibCtFEjI1MaEVH6QaSplamkX8WpoMPFC7pl2rNRhaKk6+LmBn4PqJZtYo3Qa16YPpcJvPySoUZ88gP4jVrTsxSvym/bh6hQcnMCy9oPLlNiRZN2gCHwIs4Oo2+z5/Yq6eDBz7ALptvVmU7iuoNf+LejV3DRKrtaUxSaCDf8OCe28QXbUN93jF+uvtF47Wv6MEy73xzTprfGHbUqdWr+SP8TH8a3cz8Ij9Nz4dCHtw69Ds5w/WDVGebsZThKNi1rBn3qEUTzYq+ljcybCmmO7URawwRuz66oyf8tuBKP', 'base64'));"); 
#endif
	char *_servicemanager = ILibMemory_Allocate(34701, 0, NULL, NULL);
	memcpy_s(_servicemanager + 0, 34700, "eJztff172riy8O/nefZ/0PLuWciWACFpT5uU7UMISUnz1ZCPfqQn14ADbozNtSEk283921+NPmzZlm0ZSNrdje8922BLo9FoNJoZjUbl3376V8Me3TlGfzBG1crKS9SyxrqJGrYzsh1tbNjWT//66V97Rle3XL2HJlZPd9B4oKP6SOvif9iXIjrTHReXRtVSBRWgQI59yi1t/PSvO3uChtodsuwxmrg6hmC46MowdaTfdvXRGBkW6trDkWloVldHU2M8IK0wGKWf/vWRQbA7Yw0X1nDxEf51JRZD2hiwRfgZjMej9XJ5Op2WNIJpyXb6ZZOWc8t7rUbzoN1cxthCjVPL1F0XOfr/TgwHd7Nzh7QRRqardTCKpjZFtoO0vqPjb2MbkJ06xtiw+kXk2lfjqeboP/2rZ7hjx+hMxgE6cdRwf8UCmFKahXL1Nmq1c2iz3m61iz/967x18vbw9ASd14+P6wcnrWYbHR6jxuHBVuukdXiAf22j+sFH9K51sFVEOqYSbkW/HTmAPUbRAArqPUyutq4Hmr+yKTruSO8aV0YXd8rqT7S+jvr2je5YuC9opDtDw4VRdDFyvZ/+ZRpDY0yYwI32CDfyW/mnf91oDho5Nq6ooxqnYCHPXuVh8H/619XE6gIYdKUZ5sTR6+TXiQ3M1tedgkZ+L/30r2909ACoo4836C8Xs0N34BeCd6wgPD39SpuY43X/TVfDyOQPDg+aeeEtPBhorbIRfNdxdO16I1y73Tw+w0xyedxsn9SPT2SAVpQAHTc3Dw+l9asJ9e/pP7jYxLEK+B8g5H2Alvrt2ME02caz6EAb6gWYTkfaeOCT0bjCU/FupONZ4n9FtRrKAyNa/XyUmED5sX2NBxiPJq9TcjFbjQv5i4v8UumrbViFfBn/xd6WySCLACyMzgafifBMBzDVCwX4gOHSBkoje1RYovgEQNBOI1J6KUgP3XT1CM6MSB62lj4Fgix5Ne/lVGvbE6croxtHIJF26I1HH7QukIoAjY4WnqMjLCa2bRPPI7dwRf4NNguk65nDsYNJhOdPF0/q0sjUxnjuDknLU8NarULDMBK4UUx6j8zBgfOgs0EiYDktyZQFtGvImpimB4KNEhsdU7f648HvlSiPsLoF+i8FgpFi9dyBcTUuLGH0aIFnrE/PQgWWxCEHToXSJQOLmdvDq4Jq/5fQa1RZQt/wcmBhgTzRNzircLA/ezLpysVMq99iKey276wuaXAJ6gZLDK97huMX8ADeR4ZUc1y9rTs3WCC2sZycuJR2wRH9ikn17V4gvUtrnGDm4nOhtKU7+lWhUkRrSyUsFM80k1X4WjJcwqt37lgfbjkGFtZA+4II5VdUua3Qp0p4VPgpwHmH5bxupsNYCcJYEWG0B5iJe0d0dBKAVCsBIPinAORwaqVDWAlCWAlAgIUDpjHuSjyIlUoABPz0QEw1Y/zWsMahESi8QL/hMSDjMLY3J1dXeHVaKmHB3DvFba5W95qFpcCyxOYLr7+iUD86o8hK4RH8eWit+FpyMXdBR/NYFThpHZw2L4+aWCc42MmrLEEe4BfxgI/qp+0Zof4nBepWNnBr8eCOTw8OMqNXjYdHVvbZOr2aBPXwaDagK8lAj5JJee+LGBCGjm269S4o11jdDHF5VZ3Lv5YkwD5/2fA1jEKkQNIkjvB+tIHSaOIOCp4GBjx/fLh3edA82cR6b31rK6AtZK1/3Nw/PGvOBaJ5UN/cmw/EVqsdgHGvRs/Ky6BsfgnLl3LbR/Xj+n7jbf1gBxpWbjOypmRrE4sAaE25Bpdx6ihKBH0mFLGO//b0ZOvw/CALWdaCZFnL1OYsDUbW5UwNYgGSobHo+p2lsbf14y1swzaPjg+3W3vNzCy3Fmx9LeN4Hp43j5tnzYOTDE2+DDb5MluT7Wa7jQ30SEe/lkZGRPr+R136ckvkq2dQEE1ZphybWPu9zfu6p6emXk5GeBlxxpc7+phpqyfg1yjIrb8utgJ6oilPXlyyNokGrXdBJcWWX8ewyu4gX0Sf8/ifL6JAJLXw+oXtEAf/AwpnPr8RfG1jQ7KnjTUMwMO20AXCg3+I1HpWQ11MpTaxuQpAWGkj9mQsawReL64RwyqB20cv5AwLq35jE5nYlkB/Igwif2HlUf5/8viXNr1Gy9vwdz6XDCf/LZ9SAL8dYaTGVyj3LbeBFIpjfigYtZUN4/XB9sazZ8aSSiUVPOBDjVqTvxhFsM2LuSLuoAp8+FBllaHm55UvRaLaFHNIHYQLVWvupIPHzANTKVJDlb9YWma/CfzP3Sp+s6LeBEHRq1p0GJZldSwdMGpovc/VL7VazplY4F7Lvckx/TW3nmPqXG5DFS2jV8upMQACWVfo1mpVpaGH0Vcs59GHkBpTB2OVaQB5T/D/cHXVWveK5fhE+bd7cUH/s46+4f+SoSC/+Psi/IWRgJf83X2uiOdNbeVNDg8P5usi4TbMAEVAWQlXJUQ5lvfpo5m7x1JFvzXGF5ZEkIAF28QfC+In5jgVXQ7wjJ07/4cg7+GhxXfbhwcl4tAohIWqCF/wrXQ1cMpyL1sIsFCOL2HMhyl89Vcol3g3ek8r1MJWKEpRWKOWl8GNWWMeEvyTzIcak0p4veo7+giV+Pd/8GqWQZLJZA3/C4uXoCRBfK2if6ypSb7vKE2+nzxBMwsUohyL/xerKPc0Z2pYMk25r48P22wj85FkT0AsxHyXy4fBxLqOyAh4mU1OTC9vcIe5GDhy7N6kO+abuXT6579RPkO/VNEsTFQuy7q7Uim9yscxG3K06XqESiXcrWHB23gq5fHc4p1dF2hDffpsDwfIg6GxfStcB6iCRG7GRUuw+aw5+okNuycckPeycKOZcr708Nam4IjmO0ZQPrDRht7gYqaAOVonL3C9DbZNoU3pcDr2FBXyLQt/xrYjRgALjbHuCFal2LCrm1dgYEa6KbQVoLKHMdv+KSLX3wgSC7HtIGiAbQah31EF/fordFZ4sxSsFiIOPNAAkQXYvKXw+B7QBkHD+waAvU9ROEAnF71GjjjARLu/jyv8e7CwpGzoJ6kmdBkPot9dEVQlCipc93VMVYoy2cwM4wZglKXeBpV08Le/w65jMXoEhinbY6SrThHCLhyjF5VqgDQtWdKtnntuYNsJNtYAYfre21AsUdsLREulyN+xvi6jEG3F2QwYrDM8uqbt6uHZel9ElxOjt86wDEzP5E08huIz2AzE/yU7zPgHFvm4SH4pftaWy2hTx4uCjqY6m3WGRQIcxvZUx9PC1Md5F+nWZKg74IXXTJN8JpDJhq9bhDgJPAV1QNK2dDTQXKShISxloFyZWkc3o1OP1BUXEdIpcADxnUfaq/AcgCCOAgcA2BJA6fOPUJCU/Qz//SIMtEenuC1UEe8FLX78SVsEpeUWthhGoPuLIlZFUA5zUYi3BAriXzlRVb4o5f3FkqmVeIpo2NLMvb7W737fA054XYY/L0DNpGU0sKJRh5Qq05klfu4QpbFLPotf6WrcJTZ0zIIc7J3UVhS5I2alJbv7fihFCpvBA2spi/JIZjgWPsELBCWL8METLy+WWJgF+yLpCTyHna96d1zq6VeGpWN1ZqQ74zuQmkWUxwuq5mLG+QZL74RJJDlXpIGivRBAJfBKbAPh3UD+pK9QPpnZAAmaA4L2uUhnSsCehufKYEvThzbWfHlsxwqEUgQ+QVAFfVHv6xYME+4QwPQl6xI6ODxB24enB1shlUT404/TkfDKPHQNyfcobRXGX5mZsT0QWKjkpRIA8OcBhKf4qArS2DoLF6rSlqICFtiYLqd/FXka7ViibBUfrsHFyNqk6pL1+D7GjE7i/x6d5MLs8gbAC+YTxYFb9mQFVqRBWoydiQ5iWAPVNYBCkksgCSfDcsdYs9J7W1jHCqImKkfgtCKakYfxUgnzaVh8R/yTt7cq/oSSNhqd28415ogt3GZ3bDt3oiEo+VxItgcXONtVZvfDzeaE2UsgzTZ9w9Rc1EzuYz2iUL64KP9SLl7kLnJFmNR8hl8h8NCV2dvEqZ4ytSVDEjOrI6Z3nBQIcPFGhDv37C4JxA5xJX/9xI3zcCMWSX1HG9adPrb1rLH7yOtKCrNlWDdExkkSupfOxKqP92ytF9aGJIpMuhYUo/1gC/tcRxY7NMGsZHLyw4ADBqg+GdvLbQhNAFuZbUAUwa61HXLYwMZqdH+CLXTcYzTAyi2cvRiOTB1GCXxruGpe3vYDaVxZNK2H17AeRLM65qyRNAvKcew/iz6lqEelzANMstMRZvKG5urUcs2dHJ82pa2GDawlxXlDOA6CjLNOmwyzxriiPmuqrMXYHAn2RsDx7U90MPXqpyeHl/Q4Cxh6W839+sEWexFHdYnqCU/QwMuImohIhnZnHLXc5bWuj+qmcaPnsks7pDxwTyJHQeTIJM47PjyJ625A4FS5wKGlU0RLAF0SHYMlVq12kesZ3TEG8C3QXI8u8/CNQscV2CkaIuiwDYRNFFIIzCEiCn8nwZi0FP1Oil5bK14vBfzxa9LStVWlbXnfqeTE73F1sk2VqWdsQ4E20qUYEDKXeT+uNdZzwBy/RN9oixe5+t55/WP7IrdxD80+ltDOKpyBx/AMPWptiYowfVOYGHjoNJdycbJCbI/oqcYaOdcIMqO3ES3VtYdDzQp/IafqiHDFDaKf6d4ZDP+EhnryT8Q3Fq0a3GT2Nx4LsCO6UmEnmlLlFMMNN5gzickuxCOSrdy8pxQTtxdMSmHlx7zwyyowQrQUTAi237uycS+wgmSsfDqS7q8TGtyHCoYGNLp0xGxZEHoy8ipLYZ8ueZ8utDs0JobTJ5cP9zyfY/S5oATyZdV4OMLrCBYztdoqjDSjngwEfPaoB1Pp/sKjoExsS9Y5+dqavb/9iUF8pkBHcOP+0B2PzJU5l9Mi580f3YBlA6hgFEZlCRd23inMP//0BCBIJpDy6aKEy2hvGz5OWIdxnGViZ1wQxO2F0DpguKDXkvNI3rjwdyA74leATBJchjwpIa46hNAAKXiSOYryMQu7C+DMXj4u0kskdiMR2X09iOe+/tgo1oST0EYvCVsTbBwBW/idjK0QPePJFwg/WHZ1mgQBy5c2/h7RbaAiPUIbXMslxTLpGUSGg09msQpHvDUpXXeBaD/XiDT59VdEfoTVkZi68PBNSHEbAQ0nWC3BliiWTcix7bHU6Eve9JxJafBIBMMcEyRUwCMZo4Sx/gPFCZ/gn+Rf/HsGihxa1Pd1w2Lr7Cu0r3UP20VKno5OSAMuLsLKpCwPysWrqi2m74AMI/gb/ql1dRVqchISEsX3WL1XSopfAiKz6DcdTB+8UmgjUbP54TWLx1AmMkewL0x7EZROGBA+WqBu5sNOeawOXjB9MIMLPCzoJ1ZY1NM36cKej7kk1pGIdBWBPvOCobYSYHCbmMvB312ju6yLXAt80a4WM3YjFRM1KhkpJTKJQrohQMQdS56AxRoRZBB6pt8S6cfGl0g7Kv6+swSCNUJGiGjoawogBswjHGjr/EeQeTMAhAfTFZJqdSjnJJcNcBiordnCDvgT7/9Vw9ew3MnVldE1YCdH64LQTa7Dl9EDXe+5uHn9RoMDvNjivMFSuq+7+QT6xXRkkb5tHvu7iMH8CjOEzoMiVgy6E8gSMdURZkCkmRAnesdjVWnmNjZ9Moy8TLaIzz9w6BeigBZmEIsLFTCqskBVDixyjgRZWxG3JE5NH8V4XOPXqJYqg87EmGna6Xf3eQWUyQQ4D6fKLhQyMQo9hlJTfGbzPCcpx2w+ch+0TDeOUY3hmduIUkEt7C6eD8dZxGcimkwvzIDVvYzFlGwMss8vmhjkRbqFoabkz2RDzOxbI4c/BB8Q92qoOUniav8eNgIwmBiNfBaQP9ci7hcxxL2hWZBJlg6TEK9OHSUa/gaJUYll0bOHkC+WHmaDSswBJcMqhJTv7Elumnm5pE0EGyENeMZqxKX/KI6JZOGazeSnVIhuBi3E0HfH9ig4B+3R0xT84aYgHqTgDHyk+ee1qz79SLfZEHoxQeSYbsPR3IHey4NJLvseOEgo4Tx4BO9G1IdRRI5GiDIeaJSVR7ANZIyJgaeByk6INoU8ysY4b8Jm/rVu3mEeJbMs2h7BU/B9LWBz7qEEEDyP7h0NNPpYHtJAo3KRiRlXJjETVKuUWJuEPVLGPKIYZa8eaCdvgREuylIzTYrHHmeVhU4kqJlCnE16kfilMT4kYu7p9w319DFku8eSH2z6orAIPM1JpbgLv7DKfINHXbJ6/CZy9Ru8qFGrsBzkFpJCO8+tsoIwEXEd+LWexvlL3rFJAeri+e6Js2Kl/bXRvaaSYPk6vEH2QCLfSxjiilLff/u9jwotOPfVfOM+a0Oqx4+CebTyMUd/ErJoxRTCb7WaF6ZXLyadY8KNKoKkAOsQm7yJQWqOo2WvvgnVG7h6OWt9azJkfWpA7DK9LqDon7hSh+SnDcMwSd4wxYqqxMcPPWr439IzfuSrXMzlGNKfjS/KuCIxm9i/XZaGcIlkD8P8kysiD6YqyHuFcnHpwPzS2Q+k8WtCrHzsqbSYNDZe9i7f2qd2075maXBDjJ+ti+qe9LwHCQPPs6LLQ1qWa1axCcDoNRL8ZIcgAwnonX1R+l3u6JbuGN19zXEHmhkgLJ3yjn17x5f0nf1Sw9G1sX6gwdUER/CtkK/3brSRsVot9cw4AKzavj4e2L1C/nCkW+0G63tdrU4TszpLnujS2yiat+cZmqNVFWu8n+jOXeDqi+Zt9poN27oy+or9i1asKtZs0BzOrK5aHXIskdVQbQYSG7Eqb/Eabyo2RX9lo39joFl9PUgNxapbuql7zalVqZsmnDPW61avZRljAyuSf+hto6eKq969PgEZtq8PO1gDGRgjtZrbju41IyteTZ539L6TxHlXDbW4o4/3NHfcdBzbkTVruPXe0AicwmavII1VVJs6OKlPMGQscaMy4kxzDJLl9EVYwAq1xAzhRHC3rPHLwvMiei4LMiKYwC1j2th23B3HnowizR7ZBlyeIg1S0njn2J5rjFOAjlUcVxQE9IuoWkSr+H/P19aKqBL5fwnCS6UzzZTFWMZYVUPCVJFu8qvFZMp1qCMyBi3IkWMp3JeKrNlYZGMQ5q3T2vHp3z23Jx8RskOvuN8tdI3NoEJCV1KsW7ZgEzxi1nQvapuqn9te0jjxfJb4LcEEocmN2OJrg72hOd0BPc+bv32xFt5Rl/thX6yhjjFG54bVs6eSzXuPATC7sPnQxswLjaxlynVFz00wJUO3bj7neUch/1Ph9uWLpfwXbK6nFlmPLyI3sO8lB1MZQgmQNmIoVR+NIlwQfIHLrlbjqZq1aXiC3BTiJy/vHlupRH6Sy9qhBkn3yF14mYRer5O5yoAs77wKnWpBna0AF0kUkf/fygo2Askfa/KNv87dWHchkkTvZcKFab/uMRmBjJUd3cVkfhvoTmrFcllsN0iF0NJWfVFdWVuToj3psgvAhNpSHbZAqU2XDHY9x6rwd2VVpHNRJGQxQp1ioMu0inw8RmMmExiDlC5d/FPWkz9wGaFR/zI3sWLyXR9RIYihvg7ufOmglHj5KIWUGa50s0s2RlHdw/0jPDiLGA7eLv7rj4UNSAerG9dsSFZfgEsUrrFiwxTuhV/4GdwPxwdzGRX8L//2Ki8Jf0tnCdzIh4R7r/jjZeQ08NfKBv7ndaSH2fkBgcdCSfkhrgGycUzbZG0ZmDBeP4v+n3HO38AdheLztSRe2en3hBOLKxGlc6OnV09Ptl9KgfQMF1vgdwdRWAxQZoguYUeeO1hyCSODI7AIVkRfSA+c0+GlN/t8TVGHRFEXsffY1Ijzl9B2Ap6RqBIlWe78t4VQMk7JKmi45BhRLWCvSGSMIB6ky58nIWjW3m9oiseD7ipF/aJMXmZaepLWO69xDJRNl4WvwuyPlQr+o8Co9oa8qlawOkbkkCwChLZK9P5aKDaha0/MHglBsDEaqB1wYcXk7x5I8GeuAE/SCuMk64aPPelvpco+VitiH2k9ENBLSb3LaH15ovEb8nxy6xGXHL9dIi8zV9j0u3S7QwyH9jl+ll663gQZJJQiXjzGVAnFAt478iOtcDVQOnyFs1CcCc7IXcj8ic1YcwbJRfJUwOWLEjUfnm8x7+FJzTzkAYn/BE/MLL2MTlPvvWBMxWUE4Q+tQ7le5k5kQL34Gc9nIKCU1gYRcuMExEW1Lf6+1pRW/GCJTL1xx/A/Kt6CvUpyKohPyvjBw+7GlqyS7ji1YwmHQOBJPgiSCUFU+IbIvTRYdpwevDuAmxHTM8sm4Bd37EMGMmUmehlKN+9kuYNT+rnIyejofXFvYmpYy/gVuHXuYvNo8SeQn3WG9uHxbqPvUx5/p9+RH2/fNT+WIBWluU9tcEy19sf2SXP/4qIxcRzdGns++PHFBTcp4BJvtnFKlYz8ZUug9bzsKUkAO1e/UYGEHT0OT0qHKwZVVf6lGXbn4MBgXt4g+3kcAf7zfbtHTqkX4grPxTGMWVpDrFrAHfeJx78Uj1MkMIuELDHXjbCVny7dJXp3M+QE4RIti2uR0U2/gY18TLEm/NEcGuMxnN7H4ykxJ4IQCC7kto2g7yyzCOMZCT3V69df2YKKVbbZJZq4ZEpsqUCTWZSJeFgYWyU4vorpVZQfGxefuNmdPD4QlvJ/+WJguFKGVJqXZfY8kV7WHxYHQ3h3lJx8RaHfDNnJqEdvMPdP65M3M7EhU7OuNMOcOHq9S2ON51pStS4PmE/UbYNNllgtnhv9N/RyKereDj3lMsLFfwNtz74qtBuX9cZJ6/AgZXWSOZgUMMIzFEquylxJGakED4fOvEu/vQxfXky2Jj2NOYDbic234hLw/mx8KcHdWUuEjphUHn34l6xIrj0DPCO3LAcRTcGop5vaXdzIhpEkhVN1g3SOpPOjtx3AKoE/1yrpvCcizRmQ3WC9XW/tnR43WUfaKbwiRc53C2altaO7+vhIdwy7RzuB8etNj/23s2MjM0lBor1Eb1B1Da2jlerMvMGv9NoAfLvsYzKqvOaIYuO12bVHd4VZ+7FahX68KCKp/S10LasFKw0vCZuwmH5SxJd839jcgod41gr5UwtYnWYRH6MQnUqlVONnBqU8eQ2Oycg/w6o2j11H1gWuBRMID2WYCbq2GCGb1i1wvXgIehcS6rd6fulz5Qu5xwd+JMDhlx2RIGp2hVSOXldGwZOP3uVRMXcBCqMXjj4MP+kDn3RRyIwMQCM8uZAP3OvgBVVeJLIChYDFyyhxWLxEe7Q4vSwSQM9IDmlCwzmUUVEHpfYTxH2we9Pjc4VLHawCmpdw6qp5KyBZcItoNNtgdenEgV23AKops2FET3gKGnYs1vCwBEkF1trsWm4Xksfk2YXz+fV0mTsqXeJ1ueBVSROu8MTdZCZH5PKoebAFA6qGDVasRq7eOzGIQx28GCULrwpLaBm+0pz8xjCJ/vwhIcFBgK8R3RNJr6ywXFGE4Voih2zKErzsybjgchYsAsoQWQ1fiohwoQLiKa4ueNK9sRk6QQhFDwQrUCYDYHi4UXltmGYhwbKU1g0yZ/lda29PkUfhUaAjPGq0hCdLtwH1r4X8mPIEAi4AyQkmHvc4gIaD+WSR/VEoojJ7e/qVNjHHCjM2G+98S7OJ4Hls9udDdWrptyO8yus9b3cCznAxkbyYeZtG/PgEVolLXjh7wsyqKSx0lj6FWTs0XL3gQ9SK9HJlahc4JMxI2/B+QlCJk3I/IN2gY6EcwuKvtJaGHFaJWmQhXqmYeWUFDwBQuZ2UplBEG5ORtR9rza9GgsFlgHAd17ZSCnpuW2bTBU5BRPYjefTWStFD0zfkwJ9UoI16uLP4eTFivkBqLMkTBouP2p4LnYSeGULMBZIEZAmBXY4nJdaRBojExdGZSVH8EbY1MW8c2FNI2ufffzWejJCGQPz3EHjwO1r3Gr50If6cJosgTKpIHE/xCShFCkKJ1A4qVRXValx1AfWGzih48xbS/pexJqWogwVBvUbPKxVm0QVawK8Tzbl4kL9D3TiYakChokSTY/OGKXMB6NQPQ14u3iEBj5fBOSLMAlo1zNb0BTiFibMqaWy+ZlPNHirLpCc88l2aLgamGwo4NQRpUkTTgW55dh2lquGu+xVEes/YHxXXQ8qCHkrkMc8WipSBiLE3125KKKibrTyBs3SysB4V96QHenF+RRRZWyAvCl9cHsCpCM9cfO0FGqZwNenHg7N1Mr9KUs/Mw7F0l3MuZa0lXCzJsbMnjqubN1h3xfixky1FWLZpluqRNrXwms2zqrDsCEw2BnLz8mWejAmBXcQgu17eXpLySU/Z0IhNunAJWRdu9OBRoylG13DyxI96cUFziKxWLy66wx5xq0JeBv/vfLmBpkOj69mauQjn5Dj29sjb6leuA/5ZFuWZeGH5Q80NYhf4VkGKIxSeEZ0pI0UDRwuYNwrGDWsjEEyQWhyrgVYhdcbwR0XUpQV+KcIhsAhtaYe4mFmMFUzib1AhfFvR3LiSAcbDVUgNIeEPXagX06sADloqre7F3CS6kJtE6EQ6y3mBHazi4iQ7TLH/hf0lvnxfdsneoHa5dX54vBUfxh85ARYgUDRqliUGKAx4zG9Cq3GgYeVIqJZw9namvNuxtImnykzYJY0mt/gTCSptNeY1D05OIr9KsLKC6L4hpwjkWAg74ZIDu7Ch/xKto7WXRSQp458ootuWKbMnJZgSEm3l4TgMi4YEtFNnJN3LkY83D1cpriWcCJt/FSCbL+DkUXDg8rIrGcpWFcrCIxozcJEy5G8RbgNWlNEqDmsRu9U5sAvckvxA+K3Ng1+rXd8EQ3uBuM0QL0HT98g+wexmcTShaLuoSHy58qoaN5vIcRQr5cDYmh8MFb8sJArJKpGS1WII5yIC3Iocg1Sxl7aJ7Ad2BduJFQIx4k+IZ0rZ+AfOuQpHlUlPfSZWEoO2otjHhIIFepJo78vb9BkncgRXfCDvF4vqBKcm2iB/vfaoDeGQ+M0iQkbjup4wTMFwrbhViv6Lgf72svhSJKLaIkOxeaD1I5uYigmeI+dssWqL5dc6yh8cHjQVjvrwJ5NkVVnAsuLL4yaPm3RFeCDUM66nSqgfNzcPDx8CY/W94awoH568bR4vGOMUuy0FPV2/xpOUBPuCP0Ay22KNnYTG411pMSfHEyrSW+Blq3Ha1TOzHG6Pnn3GinOPO4yos5GcWhfqMTzYP77XR5BD5TLaM6zJbZFfkgmZjDbbW9kycvllQyLOJ2xMAl3DPbbtcdDcCtArNrvgFUa04/ZET3qocbmJQfeO8odHB65uudKDWjFyWumIoYqP9fLSHlmkdTHldyA2zy+SHGn5ACmF+aOY1jVRZjxaalppo34STk7MZX4rLEsU612lXsUw4xN1RqHHJu0Un/Bg1yL0YLdx0xutsepC0j6sp94aF8sqylIvQsDEyTK6as8zV+bakLi8NNzRVdps8cokT5dEJ22KehZImXZF55Xhjt32ndXFM0sfd8sjl9Bp2enml+CKi2BxWL1gGooVmEQLhHtzpgApxym/iK24EDFTr6Xkj0dquHz9QTbrFLzSCT1MGu9op1XnFik3z4yaKdNMTFf9ELFvZJVfZ4dzyTnG9WDGNnQvjSaPdbgVUQ6yoeBFPyc43H5OYPWJ65RdWEloan3M6/fRxDfwGFdpM8bplnplT3dZkjgTYgY+oTt5PP2EnkgaUj6uzCJBknoBxIAEnaasPw/SnYQWM3VMGTmue7bDyia5v47oovbEku/kS9pO4kTP5ZabMwVE4paegqSnN6KwiTG/9MV6tqdZp5V9QJ2OP4vQ7SKwHlPHizTuJ2bnG8GRLXTkdIG0f6K+o4/QpU4Ou9WYKniBdUEh1T/qYGa8uMhhVRDmf+dz9UutlvvYbOcAaao0kp8bIG4vki+ZkCOtpELyJzYXvKArgAIZdKNjVVL0+v8gAZtsUj1NhAeeCE6X3KFEQm58Jsfc/EsF83I0wkTgbJ9p/ikMnpQ0JvoJAh97utt1jFH4bKrwWrYExUyRB2L1R2fJtJtbnC67toXIYKAVF8DL2zWIcnqGGI+uYB6F74Qt8a8c5kRRQDuQ9lQQ0drvKz4D449YYgPrUi8dN/MrhJuV+FmRh1P4VqoSSd4BQ6WceZV8fmKwFAbLhYRc7rI76BlOkOfUm5JcGpRQGPHLa3LkyppfqpK7ahSq58q/5OapX77AD8IooJlg+JfmlKF+luq5+0zE/cGm5EJSg0XzgvmWXNCohOhgYlKS+yS7S6UunIrIllgLZcmsFSuEZBkXAmfllcniOfGS7ai0PfwH9m9/Z9900vpI/Cs131tNlDf0f6h8CVdj6s5F7pcyYMMuyWLly8WL3EWuiJc7jA79JJSHjxekAPkuroxssZRCixRevL/c93LFTN3EmDyyfUc96bFpK+hJIpa8aRktII3F02rrsyu7dDGwuvo7LJhhvA/AgKFvqcykyEgkz8dwFM9EPNPHL99AKbjHA0HSc3hqQnygawHDrdUk/s/MYu2fK9IYj1xqTt/ljHJwXKutxKsK8kYSdLGYCohfnYjNh7NibhkrNQnaUDwQzAhn2LoAMfxf0K1y5ei1h+lQZkHf6wG0XzxunhSZATQbLF89/IVoqBgghjsrNDqXKYxZQERvU0yvAwvW96P9jPwToNXKo9AqSROPaUXJ4ZNhcWdJjTMo5fDErK/xrsgEocswwEI8y7ZejLEuzcjkvXxUHVnFpfoUJ5LQqMRlSvah9F5WvylxQza3MjlNM8+hdCcp2btG9BaRJ82Vy0DvcGXYKYRsS2fZUcJxQasL1ExVRs+h8kO+lRojimiWFv8KcqP3w4mfeeJu/EQ0ieE0uFgZU68cPac9MtIPmYv3N0CSZlwnGpGTeCgsLbxl1ugWHztUWF5ZXMzrTEuoyjb2P8Blk0GSZLZquI3ygm9xlGCDA2VXErtwyztx3hZyIxctj9C/8X+nOQIX652z6K708HsBEryjpJg0SWW8HHqr6cHx7yvCz6x4zKK+w/L9HLaUiMm+ThZtfzPpBNP5M9CZvgITpog+4Jdfcp7X6wPo6pEdpZWN7IbLA2oGngCL07MXovqqJcaPkSOByE2yWClkwI/DJZx+jSYkeHJQL1LaUaWYUDoi8BbpAl7kQvUPGJaURcgePfDYxE/JUDoalsDiaVI+xKQEWj/Nyh9nXNJUQ2f8naalJE8UezXL1ExME5XCAVhHwTQZ645/K7X3JlHJ8YuVJpY7MK7GfqImzDSQ/D5ZR1LN+sSAFYUWk8/50PhokpwTHVOqgmvwxtAQgx2/RRCbYfbv4xn8IWQlnwB/GWn5ZGynS1Tnu8rUgCTzHXCCNHtkpcdN85IFToz4W97By0aS26CRJOSOhSFxbGFjloWKlH5LsWihl6z+r78ySJ8rXzigqPOOxa3gOfpLfr6shCcYLDZVIRMhi6tJdzsaKV5DQg7DIhm5Xe8GkfQYaXLbmHDTGIHBwlIWdJUYoTSAhTuzGHlZRM/llan13VoiPTO0BA/JGfNiDffIa5NRA+WXwG+iBoaCoIk4SleOPSzgN0WU72iu/mJN9U6cMGKk9xguhuVx7MVnzKsXX5QvAOAwdtuHByXiZCmQV6r1udNDuVLKWYz05Aqkuc/fJUUkScptLW8pzLK5EI3VnNJWzr/xJlc09CbvBUP/9+LC/S0QgJMTXLIQg5MlFJqHCUaA0mhnP/bwv7nCZxIog/9T/vJsqUyjViIRhYsLyqfXHTjB+Zox8oBzJpSkUqNceLOOO+Lmvjz7M/f5v7kvv+WWnpX7WWL848IJxeJq6S96mjM1rITsFxmOHwvH6a+wgjA4Mg0Xi+7yntFxNOeuvKfhmgM6nV3MuPRs60akk7xJWr7ep7c3ic0KXwr0BDMkBDF66SeggRC0LHSfZAHI7HFO6BxBSeibZESjrzIdopUgEZsVBVPqrT3Ut20T6z6JxU7xK0CZ0WZpCSZ8Ut88iiv0McSV/lDIWdKERDIL4UhseDPAaozxs1ccNmY8nMItwrnigjzkN9sB+yLPu3NC0ifxFmWTnqULi+lPTJ/godd4AaF7Sfd3ke6nHLI3LGMcPGb/jfssPGLBOsEHpiCe715CB4cnaPvw9ADuGEgJly5Btk1Y9mTtJm9fLej4UwpV+fPAR0sXERMVgPPYR0olCz5oFp59RsZZ1C0Io166+viSmL88zhcxypEAYIwJAMhh9dvChsH/+PHg8HfyVnkMUslbwTGVEM1gaNVWNqzXB9sbz55ZyRG88XBmbR/RHWnr/8r//X/kBAcm6BjLTz1tOzkR5A0/H2kV6c2TSvHBaVjeYM1wYSi6Az8qATgK4/dvl6iD7G7N9AjkFPg3mlljcQruYGZQs3QxLco2rjU+h7o9xBVyMk8U4uJjQM7Klkxh/4yHZfzlWbmIcuwY0YzwRjV6+AaCLNZmhiKeohzNB4UfppwdTOA85GgWKDOyifIJ/AxhI/Aon7wvw3H7jFYUPCmpb2c8csifp4X+h1joSVx0Tv1AT0zbP4rg6g57guhC1ecAaRZAcCUihYNBFivFl0u1Ws5Tir6D2iH0DDB6Oc9ie8v0DQCEV+/5tQ0MCHN+4Razzxv89zoGOo8+MKueQp1SGIG/iHhPF4GzHquFh1/7p1s9djN8CYvGBSS7fsrvM1d+H+LqxWIXxpbcEMVFNJAMzSKQYxpKlzYxFdE8gjkeJj8Fn/sv6SkROnOAm6d7KHimVFH+JcID++Us9QxlMph0wRdfN/VMZXL1BVIz9XxlKrTvTUtqc/y3VCZGxw3kFp8DGl2WMJRZgagsTTHNz5v/K73SPEsUPFgq/OzFn7KFqrzITW6KII3SkHojyc3Ii7ivXHF3N6WxNLtLPVCXP39DW2sRcPxZEnZ7B/NPBkKVFmnGp4+02ikJ/jyNsxSO+jj7wfePOczZQnz58zTYUjiqgx2KQHzM8ZYfxBKT45Pz3+g+5o4ueJJucqFbkNDzuB1IWmIyIiRI26acYZdS2MuleCRuQCo2Uha3N4mDKx9pjHcptb1CYAuc4AihjZn7+uefyWwTbIejp9iUtMdzHUfmG70xaL1BKa2jdRRHinR3dUnYfcc4cBQU3BjRwZrfi5GU+w+CeY8b8Un/AqNFUkrh6pbTXXYmXlr9tPZJevpxiTa1OG03qVuX2gRrvFTwQefUIMLDqKB+8/IMmPttPbh7SXy+q3tIikjskqrs8pKCje4UeAIhEm3oTkYwV13bqV1c5PgvfZlGIoO7JJNhGcUnU5Jp8Unb9qKJBDMAvV9S5WUl0s9nIs51x4L4KM459bsXZgAOzyNO5kX6jCMwv6dwCJwxUVor+JNR8kpEBV7TWGIMdothQE5E1erfLi4+Y9lwMYNwULgwnj/pVxqIT0YqeOQOqCuCmpRhAGZoHp7Mu7vUgATDcWKZ+o1uzi6fMwwDPNmGAp7F0INco8jORf2GlofabU8fjQeoipbhckxkomXT9fOJl0qxpmDOC2fLLf+Oi/KsYdVaLZdUTcgfBgbbTDcuiE8GwmcoOsdyu7C7HRSXV4XFNR3a7Fc8iM8Ptgv6PVYfPAUEgpWkBuPizJaYIzG+MRo5RkqFoHAjBR9xnsEpeicFK7Hwqynus057BVZXk6sLoi6Wb9Fjuoy07YHtjJe3fPKtSygMsjJaUCQ0LgL/iFRbPNFmEHYz3DqQAamQNBJmkar7Y/Eh9vD8yPItYz25ABxMrOuIEISXswhCVJCQuJTZKQWP4oSdTeXPoFn597QKPqFHtSzCAihqS0g9EvolOw0tOwgp5IarF3PLy+TWGOQniKtDgrhNiPfgD5H1gRDjTRK5FowX3qSif0bl7kcxbBYwCMAll7BP9rcdBcVi6gOVYZDmWabz7cZx6+ik5h/OIbc4mLrFNZxqEWlFdJErX/jJEXmsbLVYKdI8BvjvpWX2p/YZ//GFHBlVuzREfFTU+R9MDUpRMvOUkfNKOUKT21WMzYupjGh8Mp9kN2Qq4TmE9dU5wc6LFvJvJyjT", 16000);
	memcpy_s(_servicemanager + 16000, 18700, "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", 16000);
	memcpy_s(_servicemanager + 32000, 2700, "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", 2700);
	ILibDuktape_AddCompressedModuleEx(ctx, "service-manager", _servicemanager, "2021-09-07T16:57:16.000-07:00");
	free(_servicemanager);

	duk_peval_string_noresult(ctx, "addCompressedModule('user-sessions', Buffer.from('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', 'base64'), '2021-12-12T15:31:14.000-08:00');");

	// Mesh Agent NodeID helper, refer to modules/_agentNodeId.js
	duk_peval_string_noresult(ctx, "addCompressedModule('_agentNodeId', Buffer.from('eJy9V21v2zYQ/m7A/+EWDJXUOHLWDQMWz8NcJ12MNM4WpSuKJiho6SRxkSmNpPyCIP99R0lOZEdOXGwYP9gmeeQ99/Yc3X3dbg3TbCl5FGt4c/jdTzASGhMYpjJLJdM8Fe1Wu/We+ygUBpCLACXoGGGQMZ++qp0O/IlSkTS8cQ/BNgJ71dae02u3lmkOU7YEkWrIFdINXEHIEwRc+Jhp4AL8dJolnAkfYc51XGip7nDbrU/VDelEMxJmJJ7RLKyLAdMGLdCItc6Out35fO6yAqmbyqiblHKq+340PBl7JweE1pz4IBJUCiT+nXNJZk6WwDIC47MJQUzYHFIJLJJIezo1YOeSay6iDqg01HMmsd0KuNKST3K95qcVNLK3LkCeYgL2Bh6MvD14O/BGXqfd+ji6Or34cAUfB5eXg/HV6MSDi0sYXoyPR1ejizHN3sFg/AnORuPjDiB5ibTgIpMGPUHkxoMYkLs8xDX1YVrCURn6POQ+GSWinEUIUTpDKcgWyFBOuTJRVAQuaLcSPuW6SAL11CJS8rprnBfmwjcy8GWKKh6nAY4C22m37spIzJgkx2rog2X1yiVF8fVjsDOZ+oTczRKmCeDUKberg2b4jLBbCRf5wjraXA6YnHNRXzdDy+X6wt36dIUpmBCkKuK25RnX4THTzNOpRMtxhxKZxgeMuED/d0ZpuQ+WG0ysDtzRaRZciGR5REpzhHun91RVafqDHp0oujtJWTBEqU0kjJI7yMLFEUFyf0P9Ng9DlAQJk9B40whaTgcyplQWSzL9CKyYBwEKi3S6EeozXJ4yFduOq1OPckxEthXjwtoEdL8+Jd1+bKPzrL82zkzI6NveZigoDt+/2YxEtwvvuFQahjH6t8DLWiW/D1NhilgV8yJhjv/XGLoSKeV8OmKWKJRFRJ1dQ2o0z0jxc+FqOEb22zPn6XqDbY0+eEHejK9Jttl/klGrcd+83JxhO1hi/CvyJNldXcNS4XHolzfBq1fFbCNsZfpZjgPflHK7R6j09mwHD92/XIbwL+swwJDliT7aLlVdQKhzKcCmbwP1voHC2RQ3CFzQEtn65dw7pZQwQh7KGTUCI1vdz0PbiPX7NS/WbChJfxvnNxj8DLOYQexyIvIp0iulbHUSI9Ngl6ZDK2p/Fd/MkW4S9NgQ1HVzWfFN7aLmAqfb6qVEMA5WChpDXHiJB8ZJ9Ta4RfIWlx2jYsaaMtxIKMLqG59/JjH39Ozkk/s+9VlyTo8ZLrA4XS4PcylR6A8K5c02tirUPN2bx+YJZpfRfSyTUreboIio4f0Ch7vXBGkq3Ba5f+Qol0QjdnWbinmobSJZy6ueTNfXFxkK8Ir9rcxiXi42+YuuPewZx8HPRour8glNVIWyt79Ps6/nma0s+8I5Mwy9FCHvQ8kmbijTqb1m/JbgNfvg+tqi50XNuM/0cUPSZTYRlyt632nb+pV+/pVyYVv7j4vfPix2TS+zJlQ/P/5gPWHwLU7a0WgzKjbYBLolgPWxyVpNY0sreWFrRaKLZ8x7xrQtN+/eZ+o15NBDovKRdU5kAIOICtTqNZ1t8kgzl5tRMEPJvHV2qpYOpkzQk15SzKtfLq4osuLrRkZaYTXExUqoT4WKOjT6eVmJnOqw0ruqQdjf57tTBfWL6vxnfkP/0jJTIqYL2U6/v/lk+/rKroyqqRCPvappPJebu+XB5g33jY3XwHjovNM0yBMkM+nvtlbr3aO3ue2qx6b7IFoa1W79A3i885M=', 'base64'));");

	// Mesh Agent Status Helper, refer to modules/_agentStatus.js
	duk_peval_string_noresult(ctx, "addCompressedModule('_agentStatus', Buffer.from('eJydVk1v2zgQvQfwf+CNFOoyQZOTvdkimwbYLLpOt01P9cJQpJFNL02qJOXECPzfO5T1QcmO064uick3H4/zZsjByTo2JDd6JSyQS2LgeyEMMFot0Wg8KCFKpyDSEDGL56DcBNdvUxqxGijy5FPsFohEFwlYy3MZu0ybFbm8JPRRqPN3lLwnjE7x49NpLnKYTil5U8d4Q+jbD1e3n65pREaE1W6Sx5RFfvO02sSAg5OsUIkTWpE0dvGfsUolGJYsCvVfNDh5HpwQ/HxaEtR490tkZAfguDbHTH8jFxF5Jm4hLC+UXYjMVR7GyNYVRo3JtrVlaIbsdi4MxOnXW+XO3328YWdRRH4noe+f8dvm6Ck0nq0UCbCLoc8cWV9E4xaYxxupY18NVUhZbTiz2f1TsfZfC/zry92E57GxwHwY7vQXZ4Sas6h2XFFMYpcsCINoz1lJpNIFnxlYMnqr1rHEkn0Gm2uFCvoMCYg1pLT26r+KbCfMoWxPT/shLKsY4EHbQrr33Z8j2onzmvWwRBzm+9Tl26+3LwAK5fXa1nVDC4Th4XpX20Cn3wswmyvfOkw/LIfYJbDuShWPy1cWHuu+ZI0xQyJDBCyb4J5m2ZV23Cwsy4Ulhq65oks+w3C48UySVToitMyDDjGiLGBE/OY26BCflm9YL7B9KXh/iRRIIpwIChyNeII94eBaKwVl0uwZZegWo2YybHvaqDxxhNJkZ4Z5tZyjFh2k0BTcm3lJo00wA8IYHSio9CXvByK8oPtEatsTeama3UwKl3xB/bH/UWQZGJ6hG1b2oi3bT2QbVhcn6rvztg9F1lrHUuqk1NayFmHPBOH80QgHzVCqkV7CQ3LWN1jyROcbhnbDdsZ0mJfuPCDc3fa6CKSF4yrxgjpcdwMrvYYrKT8K60CBsVVBX9DJIbyv6hFZHddHD3xEIYf096oyeqr4v4r4RTX8khJeVEFwNHs62LazpYKE7wjXjh4c/n5yOW+47Vza1sXGsXb8YftbLYFLPWf0Hz+h8ETI32AXpByZ3sAB57w54LnUD7HkM79RWIePDGLB3YsV6MKxvSIGBezE+qriBwnEab/s4sQFQcNw/qsfJDN4Eo41pzEk52dnZ22tg0FfjzXMBAekW4Dqj/QWsJ+oH8fllK+mMY7w3dCn3yZ39+T6bjK5ub6/+fAvbd4pe/yCI6xCQYps8R5ANaC3/euasJBACjYxInfaWNrJtqG/x2t9nFWY3joM38n7wDuim5jDSmPbZPqn0/Ln1wt+AOZR/UJ7WPlceB1XK32l0wLjwFOujbPl9VtqfrT7g1fuDzEsSd8=', 'base64'));");

	// Task Scheduler, refer to modules/task-scheduler.js
	duk_peval_string_noresult(ctx, "addCompressedModule('task-scheduler', Buffer.from('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', 'base64'));");

	// Child-Container, refer to modules/child-container.js
	duk_peval_string_noresult(ctx, "addCompressedModule('child-container', Buffer.from('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', 'base64'), '2021-12-10T12:00:08.000-08:00');");

	// message-box, refer to modules/message-box.js
	duk_peval_string_noresult(ctx, "addCompressedModule('message-box', Buffer.from('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', 'base64'), '2022-01-03T19:05:38.000-08:00');");

	// toaster, refer to modules/toaster.js
	duk_peval_string_noresult(ctx, "addCompressedModule('toaster', Buffer.from('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', 'base64'));");

	// notifybar-desktop, refer to modules/notifybar-desktop.js
	duk_peval_string_noresult(ctx, "addCompressedModule('notifybar-desktop', Buffer.from('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', 'base64'));");

	// proxy-helper, refer to modules/proxy-helper.js
	duk_peval_string_noresult(ctx, "addCompressedModule('proxy-helper', Buffer.from('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', 'base64'));");

	// daemon helper, refer to modules/daemon.js
	duk_peval_string_noresult(ctx, "addCompressedModule('daemon', Buffer.from('eJyVVU1v2zgQvQvQf5jNoZYKVU6zaA8OcnBTdyu0dRa2u0VPC0Ya2wRkUktScYLA/70z+rbjXWB1oTScr/fmkRq/9r1bXTwZudk6uLq8uoREOczhVptCG+GkVr7ne19lispiBqXK0IDbIkwLkdLS7ETwFxpL3nAVX0LADhfN1kV47XtPuoSdeAKlHZQWKYO0sJY5Aj6mWDiQClK9K3IpVIqwl25bVWlyxL73s8mg750gZ0HuBX2th24gHHcL9GydKybj8X6/j0XVaazNZpzXfnb8NbmdzZezN9QtR3xXOVoLBv8ppSGY908gCmomFffUYi72oA2IjUHac5qb3RvppNpEYPXa7YVB38ukdUbel+6Ip7Y1wjt0IKaEgovpEpLlBXyYLpNl5Hs/ktXnu+8r+DFdLKbzVTJbwt0Cbu/mH5NVcjenr08wnf+EL8n8YwRILFEVfCwMd08tSmYQM6JriXhUfq3rdmyBqVzLlECpTSk2CBv9gEYRFijQ7KTlKVpqLvO9XO6kq0RgXyKiIq/HTJ7vrUuVshdYl+nSfabgHE2Qhr73XI9DrlkU0sYFUaVcu+iiSh7XcSE8Q2F0SmAaS8w0IyW6hoPvHQaV8FH2dXSG/16qrZEaYbcLtE4YB69eAUfBbzdwzpezh3W6Jis/D4II2BVwA1WSE0BuG8EJRLFDR8ciOleDT0WbeLidbmWeUQkqVL//l1/zAUcomoBDvWBu8QWSYSKkGQejTCschX3o4WSujLfGOMTVYjkivzHCzQ2oMs95qp0Jng/XbWcD34rwMwGNHS67IJ6BQbZ1TpP2hXtz2wmc9jkZvMOhwdic9WCED8SCHYXxjF9mxAb5xanI84AK0exMiWFPHekIhcPK+YQ2co8t3aS1LKnFnr/OGgxFzugC1vbZBLHFfE1ZyHrdM9bGFrlwdKh3LOHRXqrfr0bD1FoFo2Xyx2q2+DaKThKHHZtsbwXXEVIZ/m4SES/4iOknuqrPzJ/jT/Tcpey12QPoN5vzzW1mwgnq8ejueJmNttGY/xHAnkwweQ4ui4FfaRTwiNl0LHe6Fmm4vapZdMJsWL8tv/T50KTindjy3wKDtxG8bUs0h6abNaZ/VgSyf0SjGl5Ik0pmdacTeP/u/Ts4hDVYVljUS+m8gDoMO52VOdIG/b5ddeCgKVAtUY1tUi8kvF/A95P8', 'base64'));");

#ifdef _POSIX
	duk_peval_string_noresult(ctx, "addCompressedModule('linux-pathfix', Buffer.from('eJytVFFP2zAQfo+U/3CqkJLQLim8jS4PXQERDbWIliFE0eQm19YitTPbIakY/33ntAU6eJzz4Pju8913d18SHbrOQBZrxRdLA8fdo6+QCIM5DKQqpGKGS+E6rnPJUxQaMyhFhgrMEqFfsJS2racDP1FpQsNx2AXfAlpbVyvouc5alrBiaxDSQKmRInANc54jYJ1iYYALSOWqyDkTKULFzbLJso0Rus7dNoKcGUZgRvCCTvP3MGDGsgVaS2OKkyiqqipkDdNQqkWUb3A6ukwGZ8Px2Rdia2/ciBy1BoW/S66ozNkaWEFkUjYjijmrQCpgC4XkM9KSrRQ3XCw6oOXcVEyh62RcG8Vnpdnr044a1fseQJ1iAlr9MSTjFnzvj5Nxx3Vuk8nF6GYCt/3r6/5wkpyNYXQNg9HwNJkkoyGdzqE/vIMfyfC0A0hdoixYF8qyJ4rcdhAzatcYcS/9XG7o6AJTPucpFSUWJVsgLOQTKkG1QIFqxbWdoiZymevkfMVNIwL9sSJKchjZ5s1LkVoMUJfTxytmln7gOs+bOfA5+IWSKREMi5wZ4rGCOAYv56KsvWCD2oLtemKKAvE8g3g3D99rDL+2cbwgxBrTc1KP70UzLiK99Dpw79H2YMW2C9XcCrUh4oo2RRE9r7dvlsL3MmYYBXitw08DeG4k2txqx5CGRo5pdmLhBz14+TSJLM1nSaz5/yXhIrTKo8IxXUo4uOpPLuAPsOoRpt4zrFHH3R6wWJMOjH/Q7cCsA60T+gatAvw6PurV32LWa7drm57P/dl9/RDHrUhTI1vWZmMcUX56CiJjrIGOU28qsOZmKryPzCrGzRk5fet6czbDZ0oj/VT8fxsVUqkrPwisGrrB26V3WrBrJx6NBsWT79mKqY87M9nuN7YHaIN30tSxx/Bl80rbi+W2klmZIymI/m9G07ReVdtQd52/UQCQ8A==', 'base64'));"); 
#endif

	// wget: Refer to modules/wget.js for a human readable version. 
	duk_peval_string_noresult(ctx, "addModule('wget', Buffer.from('LyoNCkNvcHlyaWdodCAyMDE5IEludGVsIENvcnBvcmF0aW9uDQoNCkxpY2Vuc2VkIHVuZGVyIHRoZSBBcGFjaGUgTGljZW5zZSwgVmVyc2lvbiAyLjAgKHRoZSAiTGljZW5zZSIpOw0KeW91IG1heSBub3QgdXNlIHRoaXMgZmlsZSBleGNlcHQgaW4gY29tcGxpYW5jZSB3aXRoIHRoZSBMaWNlbnNlLg0KWW91IG1heSBvYnRhaW4gYSBjb3B5IG9mIHRoZSBMaWNlbnNlIGF0DQoNCiAgICBodHRwOi8vd3d3LmFwYWNoZS5vcmcvbGljZW5zZXMvTElDRU5TRS0yLjANCg0KVW5sZXNzIHJlcXVpcmVkIGJ5IGFwcGxpY2FibGUgbGF3IG9yIGFncmVlZCB0byBpbiB3cml0aW5nLCBzb2Z0d2FyZQ0KZGlzdHJpYnV0ZWQgdW5kZXIgdGhlIExpY2Vuc2UgaXMgZGlzdHJpYnV0ZWQgb24gYW4gIkFTIElTIiBCQVNJUywNCldJVEhPVVQgV0FSUkFOVElFUyBPUiBDT05ESVRJT05TIE9GIEFOWSBLSU5ELCBlaXRoZXIgZXhwcmVzcyBvciBpbXBsaWVkLg0KU2VlIHRoZSBMaWNlbnNlIGZvciB0aGUgc3BlY2lmaWMgbGFuZ3VhZ2UgZ292ZXJuaW5nIHBlcm1pc3Npb25zIGFuZA0KbGltaXRhdGlvbnMgdW5kZXIgdGhlIExpY2Vuc2UuDQoqLw0KDQoNCnZhciBwcm9taXNlID0gcmVxdWlyZSgncHJvbWlzZScpOw0KdmFyIGh0dHAgPSByZXF1aXJlKCdodHRwJyk7DQp2YXIgd3JpdGFibGUgPSByZXF1aXJlKCdzdHJlYW0nKS5Xcml0YWJsZTsNCg0KDQpmdW5jdGlvbiB3Z2V0KHJlbW90ZVVyaSwgbG9jYWxGaWxlUGF0aCwgd2dldG9wdGlvbnMpDQp7DQogICAgdmFyIHJldCA9IG5ldyBwcm9taXNlKGZ1bmN0aW9uIChyZXMsIHJlaikgeyB0aGlzLl9yZXMgPSByZXM7IHRoaXMuX3JlaiA9IHJlajsgfSk7DQogICAgdmFyIGFnZW50Q29ubmVjdGVkID0gZmFsc2U7DQogICAgcmVxdWlyZSgnZXZlbnRzJykuRXZlbnRFbWl0dGVyLmNhbGwocmV0LCB0cnVlKQ0KICAgICAgICAuY3JlYXRlRXZlbnQoJ2J5dGVzJykNCiAgICAgICAgLmNyZWF0ZUV2ZW50KCdhYm9ydCcpDQogICAgICAgIC5hZGRNZXRob2QoJ2Fib3J0JywgZnVuY3Rpb24gKCkgeyB0aGlzLl9yZXF1ZXN0LmFib3J0KCk7IH0pOw0KDQogICAgdHJ5DQogICAgew0KICAgICAgICBhZ2VudENvbm5lY3RlZCA9IHJlcXVpcmUoJ01lc2hBZ2VudCcpLmlzQ29udHJvbENoYW5uZWxDb25uZWN0ZWQ7DQogICAgfQ0KICAgIGNhdGNoIChlKQ0KICAgIHsNCiAgICB9DQoNCiAgICAvLyBXZSBvbmx5IG5lZWQgdG8gY2hlY2sgcHJveHkgc2V0dGluZ3MgaWYgdGhlIGFnZW50IGlzIG5vdCBjb25uZWN0ZWQsIGJlY2F1c2Ugd2hlbiB0aGUgYWdlbnQNCiAgICAvLyBjb25uZWN0cywgaXQgYXV0b21hdGljYWxseSBjb25maWd1cmVzIHRoZSBwcm94eSBmb3IgSmF2YVNjcmlwdC4NCiAgICBpZiAoIWFnZW50Q29ubmVjdGVkKQ0KICAgIHsNCiAgICAgICAgaWYgKHByb2Nlc3MucGxhdGZvcm0gPT0gJ3dpbjMyJykNCiAgICAgICAgew0KICAgICAgICAgICAgdmFyIHJlZyA9IHJlcXVpcmUoJ3dpbi1yZWdpc3RyeScpOw0KICAgICAgICAgICAgaWYgKHJlZy5RdWVyeUtleShyZWcuSEtFWS5DdXJyZW50VXNlciwgJ1NvZnR3YXJlXFxNaWNyb3NvZnRcXFdpbmRvd3NcXEN1cnJlbnRWZXJzaW9uXFxJbnRlcm5ldCBTZXR0aW5ncycsICdQcm94eUVuYWJsZScpID09IDEpDQogICAgICAgICAgICB7DQogICAgICAgICAgICAgICAgdmFyIHByb3h5VXJpID0gcmVnLlF1ZXJ5S2V5KHJlZy5IS0VZLkN1cnJlbnRVc2VyLCAnU29mdHdhcmVcXE1pY3Jvc29mdFxcV2luZG93c1xcQ3VycmVudFZlcnNpb25cXEludGVybmV0IFNldHRpbmdzJywgJ1Byb3h5U2VydmVyJyk7DQogICAgICAgICAgICAgICAgdmFyIG9wdGlvbnMgPSByZXF1aXJlKCdodHRwJykucGFyc2VVcmkoJ2h0dHA6Ly8nICsgcHJveHlVcmkpOw0KDQogICAgICAgICAgICAgICAgY29uc29sZS5sb2coJ3Byb3h5ID0+ICcgKyBwcm94eVVyaSk7DQogICAgICAgICAgICAgICAgcmVxdWlyZSgnZ2xvYmFsLXR1bm5lbCcpLmluaXRpYWxpemUob3B0aW9ucyk7DQogICAgICAgICAgICB9DQogICAgICAgIH0NCiAgICB9DQoNCiAgICB2YXIgcmVxT3B0aW9ucyA9IHJlcXVpcmUoJ2h0dHAnKS5wYXJzZVVyaShyZW1vdGVVcmkpOw0KICAgIGlmICh3Z2V0b3B0aW9ucykNCiAgICB7DQogICAgICAgIGZvciAodmFyIGlucHV0T3B0aW9uIGluIHdnZXRvcHRpb25zKSB7DQogICAgICAgICAgICByZXFPcHRpb25zW2lucHV0T3B0aW9uXSA9IHdnZXRvcHRpb25zW2lucHV0T3B0aW9uXTsNCiAgICAgICAgfQ0KICAgIH0NCiAgICByZXQuX3RvdGFsQnl0ZXMgPSAwOw0KICAgIHJldC5fcmVxdWVzdCA9IGh0dHAuZ2V0KHJlcU9wdGlvbnMpOw0KICAgIHJldC5fbG9jYWxGaWxlUGF0aCA9IGxvY2FsRmlsZVBhdGg7DQogICAgcmV0Ll9yZXF1ZXN0LnByb21pc2UgPSByZXQ7DQogICAgcmV0Ll9yZXF1ZXN0Lm9uKCdlcnJvcicsIGZ1bmN0aW9uIChlKSB7IHRoaXMucHJvbWlzZS5fcmVqKGUpOyB9KTsNCiAgICByZXQuX3JlcXVlc3Qub24oJ2Fib3J0JywgZnVuY3Rpb24gKCkgeyB0aGlzLnByb21pc2UuZW1pdCgnYWJvcnQnKTsgfSk7DQogICAgcmV0Ll9yZXF1ZXN0Lm9uKCdyZXNwb25zZScsIGZ1bmN0aW9uIChpbXNnKQ0KICAgIHsNCiAgICAgICAgaWYoaW1zZy5zdGF0dXNDb2RlICE9IDIwMCkNCiAgICAgICAgew0KICAgICAgICAgICAgdGhpcy5wcm9taXNlLl9yZWooJ1NlcnZlciByZXNwb25zZWQgd2l0aCBTdGF0dXMgQ29kZTogJyArIGltc2cuc3RhdHVzQ29kZSk7DQogICAgICAgIH0NCiAgICAgICAgZWxzZQ0KICAgICAgICB7DQogICAgICAgICAgICB0cnkNCiAgICAgICAgICAgIHsNCiAgICAgICAgICAgICAgICB0aGlzLl9maWxlID0gcmVxdWlyZSgnZnMnKS5jcmVhdGVXcml0ZVN0cmVhbSh0aGlzLnByb21pc2UuX2xvY2FsRmlsZVBhdGgsIHsgZmxhZ3M6ICd3YicgfSk7DQogICAgICAgICAgICAgICAgdGhpcy5fc2hhID0gcmVxdWlyZSgnU0hBMzg0U3RyZWFtJykuY3JlYXRlKCk7DQogICAgICAgICAgICAgICAgdGhpcy5fc2hhLnByb21pc2UgPSB0aGlzLnByb21pc2U7DQogICAgICAgICAgICB9DQogICAgICAgICAgICBjYXRjaChlKQ0KICAgICAgICAgICAgew0KICAgICAgICAgICAgICAgIHRoaXMucHJvbWlzZS5fcmVqKGUpOw0KICAgICAgICAgICAgICAgIHJldHVybjsNCiAgICAgICAgICAgIH0NCiAgICAgICAgICAgIHRoaXMuX3NoYS5vbignaGFzaCcsIGZ1bmN0aW9uIChoKSB7IHRoaXMucHJvbWlzZS5fcmVzKGgudG9TdHJpbmcoJ2hleCcpKTsgfSk7DQogICAgICAgICAgICB0aGlzLl9hY2N1bXVsYXRvciA9IG5ldyB3cml0YWJsZSgNCiAgICAgICAgICAgICAgICB7DQogICAgICAgICAgICAgICAgICAgIHdyaXRlOiBmdW5jdGlvbihjaHVuaywgY2FsbGJhY2spDQogICAgICAgICAgICAgICAgICAgIHsNCiAgICAgICAgICAgICAgICAgICAgICAgIHRoaXMucHJvbWlzZS5fdG90YWxCeXRlcyArPSBjaHVuay5sZW5ndGg7DQogICAgICAgICAgICAgICAgICAgICAgICB0aGlzLnByb21pc2UuZW1pdCgnYnl0ZXMnLCB0aGlzLnByb21pc2UuX3RvdGFsQnl0ZXMpOw0KICAgICAgICAgICAgICAgICAgICAgICAgcmV0dXJuICh0cnVlKTsNCiAgICAgICAgICAgICAgICAgICAgfSwNCiAgICAgICAgICAgICAgICAgICAgZmluYWw6IGZ1bmN0aW9uKGNhbGxiYWNrKQ0KICAgICAgICAgICAgICAgICAgICB7DQogICAgICAgICAgICAgICAgICAgICAgICBjYWxsYmFjaygpOw0KICAgICAgICAgICAgICAgICAgICB9DQogICAgICAgICAgICAgICAgfSk7DQogICAgICAgICAgICB0aGlzLl9hY2N1bXVsYXRvci5wcm9taXNlID0gdGhpcy5wcm9taXNlOw0KICAgICAgICAgICAgaW1zZy5waXBlKHRoaXMuX2ZpbGUpOw0KICAgICAgICAgICAgaW1zZy5waXBlKHRoaXMuX2FjY3VtdWxhdG9yKTsNCiAgICAgICAgICAgIGltc2cucGlwZSh0aGlzLl9zaGEpOw0KICAgICAgICB9DQogICAgfSk7DQogICAgcmV0LnByb2dyZXNzID0gZnVuY3Rpb24gKCkgeyByZXR1cm4gKHRoaXMuX3RvdGFsQnl0ZXMpOyB9Ow0KICAgIHJldHVybiAocmV0KTsNCn0NCg0KbW9kdWxlLmV4cG9ydHMgPSB3Z2V0Ow0KDQoNCv==', 'base64').toString());");
	duk_peval_string_noresult(ctx, "Object.defineProperty(this, 'wget', {get: function() { return(require('wget'));}});");
	duk_peval_string_noresult(ctx, "Object.defineProperty(process, 'arch', {get: function() {return( require('os').arch());}});");

	// default_route: Refer to modules/default_route.js 
	duk_peval_string_noresult(ctx, "addCompressedModule('default_route', Buffer.from('eJztVttu4zYQfTfgf5gawUpKHDl2sgs0rltkc6vQxFnESRaLpghoaWQTK5NakoqcJvn3DmV5fY3bvvWhfLBM8nDmzBlyyMZ2tXIs0yfFB0MDrb3mjxAIgwkcS5VKxQyXolqpVi54iEJjBJmIUIEZIhylLKRPOVOHO1Sa0NDy98C1gFo5VfPa1cqTzGDEnkBIA5lGssA1xDxBwHGIqQEuIJSjNOFMhAg5N8PCS2nDr1a+lBZk3zACM4Kn1IvnYcCMZQvUhsakh41Gnuc+K5j6Ug0ayQSnGxfB8Wm3d7pLbO2KW5Gg1qDwW8YVhdl/ApYSmZD1iWLCcpAK2EAhzRlpyeaKGy4GddAyNjlTWK1EXBvF+5lZ0GlKjeKdB5BSTEDtqAdBrwYfj3pBr16tfA5ufr26vYHPR9fXR92b4LQHV9dwfNU9CW6Cqy71zuCo+wV+C7ondUBSibzgOFWWPVHkVkGMSK4e4oL7WE7o6BRDHvOQghKDjA0QBvIRlaBYIEU14tpmURO5qFpJ+IibYhPo1YjIyXbDihdnIrQYypqIZK4fIoxZlphrSZG6XrXyPEnJI1OksIEOiCxJ2rPB80saK7V3nYdzFKh4eMmUHrLE8Upk8IlQ55f+sUJmsEu0HvGTkuMn1wnSYZKylPtRMo8voZdohjJynXM0QXomFWUrurGJLaAzGpr/ifMu7pjiFuYeeO35CDTFRjiyv2LR3asXZurQnK7hsTtZ4t+xBDodaLZa3mSq1GVq2RSbbR0Ba9I38mMWx6hcz6fZ6JYO6n7r4tT1pp5s28yu8LDCcC3LPW82OcdzyhUF7WTU5Kiw6Z+gwthGf+C9TbS9akfJfGl0sUfb1rU43tlr859Kr+2dHe4t4pYoFlLIfIneAeyAy2Eb3n/w6vanvbqKx+DSyn8W0LJQG9jY1mjAyeRoQHE21qMsgx/sOXl5scfFHyEFHcLPMKO1/2EzrzWUNtAqxCrO5TNVNoMqZiEezrlr/o27Okw4Hv4LivC6RnzbXleHl4bmuuXf8kNBZEpQ/tDY1L4uFKeEi2y8qTSFQ55E84WoGHhIlQypujqej2MMz+jKcp1Gn4uGHjp1+N2hzx/TjVSs8LWhSql8KVwnYoYR6jsJN/RI5NcVPNGhjyLvjtNeHH7bjL1Di1U7HQhJ6R7lQAzomK1xwIVvbyzizlPKEkUPL0D3WQqlItRl+Ve4d55tLYCtZqdTK6dq8O4dbB0UA481sK5T8mRg6z25gtd7517gmJt74Sz6zRk3pzTx/eRPE7Qct0/MR5Pj5DjwS3E/wOHidnxjzWzvNSdhL2a9r6P/c+4INJrucdgl8XdjUtVWl7fSX+a2e/afzKymp2E4dMsM+WnCDN0Ro1laQ0avHYeeIvst53BWKUYyyugioLeSVMbeW+seK3MlqU/l6mt73mRRQDaaXC0xGw3G9Jyk/Tk1ORmMmCJmG90s7+k1Tgqp/gL2YXjV', 'base64'));");

	// util-language, to detect current system language. Refer to modules/util-language.js
	duk_peval_string_noresult(ctx, "addCompressedModule('util-language', Buffer.from('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', 'base64'));");

	// agent-instaler: Refer to modules/agent-installer.js
	duk_peval_string_noresult(ctx, "addCompressedModule('agent-installer', Buffer.from('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', 'base64'), '2021-11-09T11:49:08.000-08:00');");

	// file-search: Refer to modules/file-search.js
	duk_peval_string_noresult(ctx, "addCompressedModule('file-search', Buffer.from('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', 'base64'));");

	// identifer: Refer to modules/identifers.js
	duk_peval_string_noresult(ctx, "addCompressedModule('identifiers', Buffer.from('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', 'base64'), '2021-11-03T00:44:29.000-07:00');");

	// zip-reader, refer to modules/zip-reader.js
	duk_peval_string_noresult(ctx, "addCompressedModule('zip-reader', Buffer.from('eJzVG/1T20b2d2b4Hza5mVpujDE2oS0+2iMYrkwJZDC5TC9lMrK0wgJZ0kmrAs1wf/u9t7uSVquVZJLeD6XTGPbjvbfv+71db3+7uXEUxY+Jf7NkZDwaj8hpyGhAjqIkjhKb+VG4ubG5ceY7NEypS7LQpQlhS0oOY9uBDzkzIP+iSQqryXg4IhYueCmnXvanmxuPUUZW9iMJI0aylAIEPyWeH1BCHxwaM+KHxIlWceDboUPJvc+WHIuEMdzc+FVCiBbMhsU2LI/hL09dRmyG1BL4WTIW729v39/fD21O6TBKbrYDsS7dPjs9Oj6fH28BtbjjfRjQNCUJ/U/mJ3DMxSOxYyDGsRdAYmDfkygh9k1CYY5FSOx94jM/vBmQNPLYvZ3QzQ3XT1niLzJW4VNOGpxXXQCcskPy8nBOTucvyZvD+el8sLnx4fTq54v3V+TD4eXl4fnV6fGcXFySo4vz2enV6cU5/HVCDs9/Jb+cns8GhAKXAAt9iBOkHkj0kYPUBXbNKa2g9yJBThpTx/d8Bw4V3mT2DSU30e80CeEsJKbJyk9RiikQ525uBP7KZ1wJ0vqJAMm325sbv9sJOb44ml2SA7Izwv/Gr/emYlyMTiZ7kx/Gu9/LwbMTHNz7bjLe/e71eIrsx+E4iQA5hSkpBasnh3p9udPN4oA+qCuAn9Re9frDGZ/iwLwsdJBkAkJ37k6iAOh+Z7Ol5dKU9Tc3PgsF8T1iAQIHODeMA5sBg1bk4ID07v1wMu71xSq5GH9wO+DGj2EKbGZWbxsw30Z+aPV++41TieuexAcNUroODNwpgWzrMPDQLLoDblc3tdBNfiIIkuwTBVwJbWFzFgugw3Tpe8zKV90vwR4tORXQ8AZs8EeyU+cEB/LqgFjrE0JeaThzpEIU1otCpF4K/KAPYCrp/DF0LETW75eLFTrwp7pvdef6SblNwfFUMPZJURH6wBLbYefwacWabgxjGrpgFjkr4GijPvlM4mEaZYlDh04QpRS4ByOfwADxt4SyLAmnqvxCgA0cL8HFUVywnGu10Il4iCQLdQVuPYu1iGP611FrB/xJFNChH3rRjtU7FkIA3sCB4DTcTuUOljzWsBntWsNgM2dp0fppa8JTp0CKt1ZFa6Q8C9jiF1gpHA8XmwR4Q9mcD1oojRwILI0yFmdM9VpcVR1Yy+gHCCNU7sNzDEDBvMC+SYEV94seeaoBGob2Ck1YkXk5VzrRuDYXgSj4qXsDUhiAVWcRahCG5xxYflig2CEvDkhlLudm4og9SFyLtVbhIrt7R5dH5AglSjwb/I/bU/lfk4EiY66Niv2qsAt1UNgnTxH7MbUKruD8UyVq/OHHMXUvFrfUAZgY/0u/wFF8EnOnM+ByD1ZvAVhQxaG6sTdVN3Ao6HXxU85gPLbQ/G8fMJ3IEWk8E/tPZvnmj7cP10PPVZixAOx3VeUXJAxd6vkhfZdEENbZI2fPgPQw50pBAxQkoLn7dY0wSA/JBWkAMR+vp/UpXxuDI1o+P1zJhX51iYZAinsYZ+nSUnZ99K+FZmkYnkyaQiz4NLn+vioUVOaD8tSowBxDTQQ5UJUcXHmNIAo9U0EXnkBFoLgHIxrOQPCH5IDUMFVCJbFe4DqMRGyZRPfE6mFS7UWQoIHtlF5KkdhUHUMQCGGI+TYmjkgeD26Ngtcc9nkE9UGx94XJYrmDupcZm9UpdMymqaqEzjIL78BPBaAJ/fp6AwgDne/sNN2+WiaZiCscZh7NX8HQ4pGBLejU5z+KmsC/k3FOUz7etA+yGSFB1OKLXwzUt5ygQCx35+rALYIT0IRWoCbr4O7Ajz+c8VYbrpJSvhYFngVBy4an5qkyqfgCUqtE8M/nU9Ew3ExYt/xEujezmb2m9NY/iIHap0F9DPy/HahW9UxzWlOduljBT4/K0Xb4doX788UD0oEqHKwryejXcRnDv8rk1P9DD3MtFOnG3kwOL88KgSjaVZRqI/LNN1WJVdxHfWdei/WbfdnXakDhDZt0bw0YJZ9yLSF/Fb9EV1gMuYnth42BpoWIhmGDYmqJkSEkDxeZ59HkbeRiLrpjWKDqhinJE2tyxfJs4I5pRW5ajQtaBIPzIuaODDOxnWlFm1oLVIRVaxNUKrA89aHuVtFCEgXZjOZzkL1+Jh/enF7N98nWzusqTxW8/EgydcOPqTaF/mHuh3dqNlgMWjRJBiIRuYQh+JULab1UbGz1EA5PbgoQZWZDPh5fXorUB7Dg+DUk/gXqYUoDb/iJLz2jHtNVpnEh2Too0bVv4nmdJc40TLH7ao2U4/YNlWjDufFne5vMopCSD2XvlXtf3pXkfZZFEDl39Y2FEyrPoCW8LVh1rkNiObs4PyZ4AOyaXmErOwG1Seq1q0I5JM1vo4QSNK6WUADGY4yCBk9g9k4NJwAKCvTkKuLUd58VNOwSbIamyO8SgFAqjaUDrTxu4kW1DYLyk8LhRYnnYgtEaMx+joL/NSAi0O3riCHy7Y5+2CM/iY/6vClqV5heaO0anK+6gSZj5+rPPQ7Tl4CK2sHPvGdQcwva3Fd5h53c7pCm96chm4zPjq1RH1X/7ORSP6qm4v+kIU3sgLzLkjhKKTkJbNme06Du7AHUvVoGoYFT6kXylrJl5DYD+74L2AkkQufYBTuTCtEEadxJF+89dsOpk6SLSjpJrsMgziK+YHvrQAfKJbGz26/U5QbqFNtjkUw0kUarAXuzSXRt6GsFsR49aybbdn6w4bX9Um7pTQQK0zfvzf3Bn86NBnxgCD7aaSMAfvrI81LINl6RyahZLZsVzVR0NOEzua2aSzJtNXqmKITwLHPVhsBsaueqQZUonry0gTlURFPjTs4C2PZG8MIOgFgLhdRhtmd4JiIORf6hRCNFAh0gyNaPRRcL0lWkERu69bhWT6XXj1+Vc01GkO+UOlQjGAUtBjWRVcRbv56oNDwrnUjZHj8MAjXKlKP8ykHcpJjvAbpvkgxawXuUJWRguBNk8LfV28dbIexclrN4VSGROPeQ+sj7HzE5VYPvuqm+jj5ldsLSDz5b8quor0Bf/qG3u8vOOHY95SWEVZoQKNkAFtyKrq3IOFJuhOm0GLjlA7fTqozxOMppID8UZ1n7frB2YJU32QKfJIQ3mJYr47KlsEV2+lO91hFXPtLWNReiXGGqeLRVsszUS0x+JVJeGPCbiraLX1ZeqRbtDb7po3+t1WqK6ig3Rto9QZc1fSoNR7Ty03EleatPo5BWjZa1EtRW7pcbjwv5u+eHfrqkWtrOAfH7Z/4b/JIFLNWdlmha1+48y2Oba1sVpODyZ4JXEvtEoZ9faFeVtrqP0jurPyyuTKuXJJZprX7Z0wxvKMwOF6y3Hq9BXahhKiHOWbvgnh1eHcrrBCk4nZ9lzwyiAEqVtz47i1tlSx42ALVjM+ujc63j0GqSeiX4PATF1IC04lpTxpzHYJprZhEah49DF591OVmS0JCRvD9jUmj0ARoFRUpRnKlp4x8mg7byaWPMbQiucrM5xspJw0X7/zFo4Pm4ceaM4H9oCyTndDfMeVN3700u8NleFDOms5Ya2DDfXgebb1FNcIayP1duMOaVWF+SS97RwSyxonuIAZmJHupMOm5jeTeu5LB68knI1xTYdWhQZm5Nxmg3avFJ3KJZ01CBdkD94qq9GZbMtVuo6oL0Xj1hO6zxuAPWmZ0y8jZy8Ukkf+hIrvyVGSA/5c7o2QBnNmsD2EWh6FCc+DRwn9unMFQqXTVKVbX7WJKUDomC4cHfa78TgBFuSud2zlJvyKK5SDhrHZUvoBakoVE4BhLhf2/cHXBbVZS3Sef2Kg5yyscl6b0lfejV6H/qDhlGnyTrccXdyhhUWaq6SG3q616UNBPWdKfxbDFpha+x5DUQoLJQYQzPYitvd0Raa0yziwdUL2p5WK2eqLzOQ5j8IWkOwZiAiKdZhnus6sMvbUH5FrWCmP4O+Q4iP8Zfjlc+Y5ijAQ85EZXnS5hh/beaX+VZQvEs9an+uI2LK7bZsnzT9n/KQjjzH2MaYa2DCHl9KopNwytUXrQ3vgHmANpLQfGYkLsb9TmUqdQxPRDTe2Y0rD3YTJnNSmqGabWpJS423dquCGrUYtdA1zJwQcwGoQ8vPl3OLs7Pfl3nta1CIQKVZYiRls+kuI4BN+alMj2e0dRJ/JhFSW9QmKwgsDTTUeG7CkUFsI5bcUX497Oys9wkPqt9Svm0QYa08j1De4/JFPD7aOhHs0vUVqStaJTe4nOwBBzHcZLAsetvtVUi25K7PNBO69uo17oJ3KBhE+Qy7bvGpl3yEXDlZnV3b0B29/A5eBnAlXjbddlwBE4HE9IZ6KcDqvEoU+D9mhnVI6f88k9zZrLbdREDI2/90F9lq6+4ZYKRMtcwPRhtS5zfPyNxXoeS56Sp67Bn7QR6PFoDGveUz85+x18OuSMNXocD+NW0JAQVPWTye1RpM8TJOjKC5HotiJyvk86LSRip3EzYrBnabp2X67k4UD50cvrrBVPHp0z4yEF9umFX/iP6fPhvy/V59ZJnv0kfWyCo9WoLjN02GCKZNO8EPrfs9KCiVeKE8eZSPyt3dA3FYdv2xDFTCAI173rS9EOLVEWrS4sClQiAj3+84tci2DSmP5WgmVqYExq+EaFXN+LdV6QnB3zkWemBeEej9Vv8SuiS1wQHZDyaEp/8nVTShil59cpvzxssgURnXDX72CJ+v6/nFpDC8q86Pru4PMF0lMzxJgpDzfHRLO8voXvwlTdbHiTghIoWKBbjxidFGvStpp91NhMI+Okd+ZvwVII1XbHbCOY8Wy1AHYHupkwixW+98m/+un7qNCOs+1kjwquIAZKwG20zpp16qDSi4rfDJhRG0MKqx/qbik4szXxLl1EWuGSB33AuvF0jZhH56ghNzzi4iZber6mEbzhhpa5voIW/X1DRwG8mdqjfZMp/TPevFZ+TQ608aCi9kWmuxgNZLrV0mUbaOWX9tUVgCo9XIuyXJEBxkRea1SLcT//tx1oVjn6ps9wEXzfhX0HN61j+wrZf+bppdwnaSxaFOvIvoKU8Y9MfYahLyselOmz+AgVhI/8EJORjpYrsG1lfdlg8t1RXnvSU2A7ILr5pF4A/jq75JenD65EyuCMHd98og2M5OJoog5N8cLflW15JVn55UBVlwWwpzVXkZmC79CGOEn6H8lm+z0p4jOMy3hcfoLH/Az9eiek=', 'base64'));");

	// zip-writer, refer to modules/zip-writer.js
	duk_peval_string_noresult(ctx, "addCompressedModule('zip-writer', Buffer.from('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', 'base64'));");

	// update-helper, refer to modules/update-helper.js
	duk_peval_string_noresult(ctx, "addCompressedModule('update-helper', Buffer.from('eJytVd9v2zYQfheg/+GaF8mdK2d5jNEHL00xY4UzREmDdhgCWjrJzGSSI6m6XuD/fUdRtiX/QPcwvYgi77777rs7avQ2DG6kWmteLixcXV5dwlRYrOBGaiU1s1yKMAiDTzxDYTCHWuSowS4QJopl9GpPhvAZtSFruEouIXYGF+3RxWAcBmtZw5KtQUgLtUFC4AYKXiHg9wyVBS4gk0tVcSYyhBW3iyZKi5GEwZcWQc4tI2NG5oq+iq4ZMOvYAj0La9X1aLRarRLWME2kLkeVtzOjT9Ob21l6+47YOo9HUaExoPHvmmtKc74GpohMxuZEsWIrkBpYqZHOrHRkV5pbLsohGFnYFdMYBjk3VvN5bXs6balRvl0DUooJuJikME0v4JdJOk2HYfA0ffj17vEBnib395PZw/Q2hbt7uLmbfZg+TO9m9PURJrMv8Nt09mEISCpRFPyutGNPFLlTEHOSK0XshS+kp2MUZrzgGSUlypqVCKX8hlpQLqBQL7lxVTRELg+Dii+5bZrAHGdEQd6OnHjfmAalJbkivN9qGEftVuTKHwZFLTIHBMYybeNa5czi78wuBmHw6kvmcDRawhC42iLGO8eYkhySwcsAXpv+SZ5pp4loxruNl2bjZQwbF9fB8gLiNztW/3D1TiOjXKJBws1XrrpcHDRRaJDjwdita92EtvS18YCtActPJN2Dd4su+vi0f0Kiin2ez95jgRXVIyZAhfnAe7ZCbcVS/7dUW7l80MTNp0kqFCVN45v38PNgb9ah4h7VAMbRo6BuxMx1eCbpJhHWuGkhwGbao24k97SRskoS/+7hZr/EyuDZwFav+xsH555cjsZ2y1QYKk9GNbD4RIOMqaX1slMr+Ami51p4etGQZCwqVppriFbzqC/YAVv3ZMxmC4hx8ENqZ/M/EBZPnW27U/2Ajs8/cW1CIqjxyVPPhM794rSRFHHUcCVJ9t2267KDbPymC7sbqCPlWpcSbVuDbu/9cfnnIFFcYezjn2mQIx02rfAHkxUfj1GvfQ7q0++WWlRc/JWuRXZipE+7uD/UR8rjwOmwt/4r3EkGfbAzAjX92GvHo1TtmT7z2p7V3Rc2yqXYz/ZOfR92Lz92rtcmlG+H3a24v2rDYOP2lzKvK0zoSpHauvvr1f8+rv0LNmT4L6QVhQk=', 'base64'));");

#ifndef _NOHECI
	duk_peval_string_noresult(ctx, "addCompressedModule('heci', Buffer.from('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', 'base64'));"); 
#endif

#ifdef __APPLE__
	duk_peval_string_noresult(ctx, "addCompressedModule('mac-powerutil', Buffer.from('eJztVk1v00AQvVvyfxjlYgdSp+qRiENog7BAiVQXEKIIbdaTeMHeNbvjuhHqf2fWcdMUAhISHxLCF9uzzzNv3r5ZefwgDE5NvbFqXRCcHJ8cQ6oJSzg1tjZWkDI6DMLghZKoHebQ6BwtUIEwrYXkW78ygldoHaPhJDmG2AMG/dJgOAmDjWmgEhvQhqBxyBmUg5UqEfBaYk2gNEhT1aUSWiK0ioquSp8jCYM3fQazJMFgwfCa31b7MBDk2QJfBVH9aDxu2zYRHdPE2PW43OLc+EV6OptnsyNm6794qUt0Dix+apTlNpcbEDWTkWLJFEvRgrEg1hZ5jYwn21pFSq9H4MyKWmExDHLlyKplQ/d0uqXG/e4DWCmhYTDNIM0G8GSapdkoDF6nF88WLy/g9fT8fDq/SGcZLM7hdDE/Sy/SxZzfnsJ0/gaep/OzESCrxFXwuraePVNUXkHMWa4M8V75ldnScTVKtVKSm9LrRqwR1uYKreZeoEZbKed30TG5PAxKVSnqTOC+7YiLPBh78VaNlh4DtWnRNqTKeBgGn7f74Dc6eb9YfkBJ6Rk8hqgS8miHjCa3G9YBXYlYM2iXsgv4dB7Sp/TXlbAgC1Xmk7uYY9fIAuLaGsl6JHUpiNuuhneQvQz+koKViXJhW6WjR/fXunVfgen0voijLvC+LxANE7xG+ZRdHEfjpdJjV0QjeBvx7d1w8p10iaPcNMQ369WIJvfDRsdMiAQn2okQy6LRH4fwuReJv3z4GLpgQiZjT+l1PJzAzQ+LorWHivrw7yuqdOInhQUyTjhpFY/6EcJlxIdMeTtjXb1BtnGEFcyuUJMb+DHrNv8yutR4rehSR9+v1ApFMwbFhyBLi+LjV/EcV6Ip6cCeU2FNC3HUO687sfxYYcW8toPbHV637jrI6uuSN9vHmz2r88iSsLRv9j70U3b/7/bDRf+y213RcIethiPLDmr/tHl3Tvpt9t01uH9Y97H/5/U/5eDibzj4zku/2sI3/o+jMnlTIvuB/3LJscYa2/3/l8kX1l4yWQ==', 'base64'));"); 
#endif
}

void ILibDuktape_ChainViewer_PostSelect(void* object, int slct, fd_set *readset, fd_set *writeset, fd_set *errorset)
{
	duk_context *ctx = (duk_context*)((void**)((ILibTransport*)object)->ChainLink.ExtraMemoryPtr)[0];
	void *hptr = ((void**)((ILibTransport*)object)->ChainLink.ExtraMemoryPtr)[1];
	int top = duk_get_top(ctx);
	char *m;
	duk_push_heapptr(ctx, hptr);										// [this]
	if (ILibDuktape_EventEmitter_HasListenersEx(ctx, -1, "PostSelect"))
	{
		ILibDuktape_EventEmitter_SetupEmit(ctx, hptr, "PostSelect");	// [this][emit][this][name]
		duk_push_int(ctx, slct);										// [this][emit][this][name][select]
		m = ILibChain_GetMetaDataFromDescriptorSet(Duktape_GetChain(ctx), readset, writeset, errorset);
		duk_push_string(ctx, m);										// [this][emit][this][name][select][string]
		if (duk_pcall_method(ctx, 3) != 0) { ILibDuktape_Process_UncaughtExceptionEx(ctx, "ChainViewer.emit('PostSelect'): Error "); }
		duk_pop(ctx);													// [this]
	}

	duk_get_prop_string(ctx, -1, ILibDuktape_ChainViewer_PromiseList);	// [this][list]
	while (duk_get_length(ctx, -1) > 0)
	{
		m = ILibChain_GetMetaDataFromDescriptorSetEx(duk_ctx_chain(ctx), readset, writeset, errorset);
		duk_array_shift(ctx, -1);										// [this][list][promise]
		duk_get_prop_string(ctx, -1, "_RES");							// [this][list][promise][RES]
		duk_swap_top(ctx, -2);											// [this][list][RES][this]
		duk_push_string(ctx, m);										// [this][list][RES][this][str]
		duk_pcall_method(ctx, 1); duk_pop(ctx);							// [this][list]
		ILibMemory_Free(m);
	}

	duk_set_top(ctx, top);
}

extern void ILibPrependToChain(void *Chain, void *object);

duk_ret_t ILibDuktape_ChainViewer_getSnapshot_promise(duk_context *ctx)
{
	duk_push_this(ctx);										// [promise]
	duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "_RES");
	duk_dup(ctx, 1); duk_put_prop_string(ctx, -2, "_REJ");
	return(0);
}
duk_ret_t ILibDuktape_ChainViewer_getSnapshot(duk_context *ctx)
{
	duk_push_this(ctx);															// [viewer]
	duk_get_prop_string(ctx, -1, ILibDuktape_ChainViewer_PromiseList);			// [viewer][list]
	duk_eval_string(ctx, "require('promise')");									// [viewer][list][promise]
	duk_push_c_function(ctx, ILibDuktape_ChainViewer_getSnapshot_promise, 2);	// [viewer][list][promise][func]
	duk_new(ctx, 1);															// [viewer][list][promise]
	duk_dup(ctx, -1);															// [viewer][list][promise][promise]
	duk_put_prop_index(ctx, -3, (duk_uarridx_t)duk_get_length(ctx, -3));		// [viewer][list][promise]
	ILibForceUnBlockChain(duk_ctx_chain(ctx));
	return(1);
}
duk_ret_t ILibDutkape_ChainViewer_cleanup(duk_context *ctx)
{
	duk_push_current_function(ctx);
	void *link = Duktape_GetPointerProperty(ctx, -1, "pointer");
	ILibChain_SafeRemove(duk_ctx_chain(ctx), link);
	return(0);
}
duk_ret_t ILibDuktape_ChainViewer_getTimerInfo(duk_context *ctx)
{
	char *v = ILibChain_GetMetadataForTimers(duk_ctx_chain(ctx));
	duk_push_string(ctx, v);
	ILibMemory_Free(v);
	return(1);
}
void ILibDuktape_ChainViewer_Push(duk_context *ctx, void *chain)
{
	duk_push_object(ctx);													// [viewer]

	ILibTransport *t = (ILibTransport*)ILibChain_Link_Allocate(sizeof(ILibTransport), 2*sizeof(void*));
	t->ChainLink.MetaData = ILibMemory_SmartAllocate_FromString("ILibDuktape_ChainViewer");
	t->ChainLink.PostSelectHandler = ILibDuktape_ChainViewer_PostSelect;
	((void**)t->ChainLink.ExtraMemoryPtr)[0] = ctx;
	((void**)t->ChainLink.ExtraMemoryPtr)[1] = duk_get_heapptr(ctx, -1);
	ILibDuktape_EventEmitter *emitter = ILibDuktape_EventEmitter_Create(ctx);
	ILibDuktape_EventEmitter_CreateEventEx(emitter, "PostSelect");
	ILibDuktape_CreateInstanceMethod(ctx, "getSnapshot", ILibDuktape_ChainViewer_getSnapshot, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "getTimerInfo", ILibDuktape_ChainViewer_getTimerInfo, 0);
	duk_push_array(ctx); duk_put_prop_string(ctx, -2, ILibDuktape_ChainViewer_PromiseList);
	ILibPrependToChain(chain, (void*)t);

	duk_push_heapptr(ctx, ILibDuktape_GetProcessObject(ctx));				// [viewer][process]
	duk_events_setup_on(ctx, -1, "exit", ILibDutkape_ChainViewer_cleanup);	// [viewer][process][on][this][exit][func]
	duk_push_pointer(ctx, t); duk_put_prop_string(ctx, -2, "pointer");
	duk_pcall_method(ctx, 2); duk_pop_2(ctx);								// [viewer]
}

duk_ret_t ILibDuktape_httpHeaders(duk_context *ctx)
{
	ILibHTTPPacket *packet = NULL;
	packetheader_field_node *node;
	int headersOnly = duk_get_top(ctx) > 1 ? (duk_require_boolean(ctx, 1) ? 1 : 0) : 0;

	duk_size_t bufferLen;
	char *buffer = (char*)Duktape_GetBuffer(ctx, 0, &bufferLen);

	packet = ILibParsePacketHeader(buffer, 0, (int)bufferLen);
	if (packet == NULL) { return(ILibDuktape_Error(ctx, "http-headers(): Error parsing data")); }

	if (headersOnly == 0)
	{
		duk_push_object(ctx);
		if (packet->Directive != NULL)
		{
			duk_push_lstring(ctx, packet->Directive, packet->DirectiveLength);
			duk_put_prop_string(ctx, -2, "method");
			duk_push_lstring(ctx, packet->DirectiveObj, packet->DirectiveObjLength);
			duk_put_prop_string(ctx, -2, "url");
		}
		else
		{
			duk_push_int(ctx, packet->StatusCode);
			duk_put_prop_string(ctx, -2, "statusCode");
			duk_push_lstring(ctx, packet->StatusData, packet->StatusDataLength);
			duk_put_prop_string(ctx, -2, "statusMessage");
		}
		if (packet->VersionLength == 3)
		{
			duk_push_object(ctx);
			duk_push_lstring(ctx, packet->Version, 1);
			duk_put_prop_string(ctx, -2, "major");
			duk_push_lstring(ctx, packet->Version + 2, 1);
			duk_put_prop_string(ctx, -2, "minor");
			duk_put_prop_string(ctx, -2, "version");
		}
	}

	duk_push_object(ctx);		// headers
	node = packet->FirstField;
	while (node != NULL)
	{
		duk_push_lstring(ctx, node->Field, node->FieldLength);			// [str]
		duk_get_prop_string(ctx, -1, "toLowerCase");					// [str][toLower]
		duk_swap_top(ctx, -2);											// [toLower][this]
		duk_call_method(ctx, 0);										// [result]
		duk_push_lstring(ctx, node->FieldData, node->FieldDataLength);
		duk_put_prop(ctx, -3);
		node = node->NextField;
	}
	if (headersOnly == 0)
	{
		duk_put_prop_string(ctx, -2, "headers");
	}
	ILibDestructPacket(packet);
	return(1);
}
void ILibDuktape_httpHeaders_PUSH(duk_context *ctx, void *chain)
{
	duk_push_c_function(ctx, ILibDuktape_httpHeaders, DUK_VARARGS);
}
void ILibDuktape_DescriptorEvents_PreSelect(void* object, fd_set *readset, fd_set *writeset, fd_set *errorset, int* blocktime)
{
	duk_context *ctx = (duk_context*)((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[0];
	void *h = ((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[1];
	if (h == NULL || ctx == NULL) { return; }

	int i = duk_get_top(ctx);
	int fd;

	duk_push_heapptr(ctx, h);												// [obj]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);		// [obj][table]
	duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);						// [obj][table][enum]
	while (duk_next(ctx, -1, 1))											// [obj][table][enum][FD][emitter]
	{
		fd = (int)duk_to_int(ctx, -2);									
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Options);	// [obj][table][enum][FD][emitter][options]
		if (Duktape_GetBooleanProperty(ctx, -1, "readset", 0)) { FD_SET(fd, readset); }
		if (Duktape_GetBooleanProperty(ctx, -1, "writeset", 0)) { FD_SET(fd, writeset); }
		if (Duktape_GetBooleanProperty(ctx, -1, "errorset", 0)) { FD_SET(fd, errorset); }
		duk_pop_3(ctx);														// [obj][table][enum]
	}

	duk_set_top(ctx, i);
}
void ILibDuktape_DescriptorEvents_PostSelect(void* object, int slct, fd_set *readset, fd_set *writeset, fd_set *errorset)
{
	duk_context *ctx = (duk_context*)((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[0];
	void *h = ((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[1];
	if (h == NULL || ctx == NULL) { return; }

	int i = duk_get_top(ctx);
	int fd;

	duk_push_array(ctx);												// [array]
	duk_push_heapptr(ctx, h);											// [array][obj]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);	// [array][obj][table]
	duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);					// [array][obj][table][enum]
	while (duk_next(ctx, -1, 1))										// [array][obj][table][enum][FD][emitter]
	{
		fd = (int)duk_to_int(ctx, -2);
		if (FD_ISSET(fd, readset) || FD_ISSET(fd, writeset) || FD_ISSET(fd, errorset))
		{
			duk_put_prop_index(ctx, -6, (duk_uarridx_t)duk_get_length(ctx, -6));		// [array][obj][table][enum][FD]
			duk_pop(ctx);												// [array][obj][table][enum]
		}
		else
		{
			duk_pop_2(ctx);												// [array][obj][table][enum]

		}
	}
	duk_pop_3(ctx);																						// [array]

	while (duk_get_length(ctx, -1) > 0)
	{
		duk_get_prop_string(ctx, -1, "pop");															// [array][pop]
		duk_dup(ctx, -2);																				// [array][pop][this]
		if (duk_pcall_method(ctx, 0) == 0)																// [array][emitter]
		{
			if ((fd = Duktape_GetIntPropertyValue(ctx, -1, ILibDuktape_DescriptorEvents_FD, -1)) != -1)
			{
				if (FD_ISSET(fd, readset))
				{
					ILibDuktape_EventEmitter_SetupEmit(ctx, duk_get_heapptr(ctx, -1), "readset");		// [array][emitter][emit][this][readset]
					duk_push_int(ctx, fd);																// [array][emitter][emit][this][readset][fd]
					duk_pcall_method(ctx, 2); duk_pop(ctx);												// [array][emitter]
				}
				if (FD_ISSET(fd, writeset))
				{
					ILibDuktape_EventEmitter_SetupEmit(ctx, duk_get_heapptr(ctx, -1), "writeset");		// [array][emitter][emit][this][writeset]
					duk_push_int(ctx, fd);																// [array][emitter][emit][this][writeset][fd]
					duk_pcall_method(ctx, 2); duk_pop(ctx);												// [array][emitter]
				}
				if (FD_ISSET(fd, errorset))
				{
					ILibDuktape_EventEmitter_SetupEmit(ctx, duk_get_heapptr(ctx, -1), "errorset");		// [array][emitter][emit][this][errorset]
					duk_push_int(ctx, fd);																// [array][emitter][emit][this][errorset][fd]
					duk_pcall_method(ctx, 2); duk_pop(ctx);												// [array][emitter]
				}
			}
		}
		duk_pop(ctx);																					// [array]
	}
	duk_set_top(ctx, i);
}
duk_ret_t ILibDuktape_DescriptorEvents_Remove(duk_context *ctx)
{
#ifdef WIN32
	if (duk_is_object(ctx, 0) && duk_has_prop_string(ctx, 0, "_ptr"))
	{
		// Windows Wait Handle
		HANDLE h = (HANDLE)Duktape_GetPointerProperty(ctx, 0, "_ptr");
		duk_push_this(ctx);													// [obj]
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_HTable);	// [obj][table]
		ILibChain_RemoveWaitHandle(duk_ctx_chain(ctx), h);
		duk_push_sprintf(ctx, "%p", h);	duk_del_prop(ctx, -2);				// [obj][table]
		if (Duktape_GetPointerProperty(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT) == h)
		{
			duk_del_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT);
		}
		return(0);
	}
#endif
	if (!duk_is_number(ctx, 0)) { return(ILibDuktape_Error(ctx, "Invalid Descriptor")); }
	ILibForceUnBlockChain(Duktape_GetChain(ctx));

	duk_push_this(ctx);													// [obj]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);	// [obj][table]
	duk_dup(ctx, 0);													// [obj][table][key]
	if (!duk_is_null_or_undefined(ctx, 1) && duk_is_object(ctx, 1))
	{
		duk_get_prop(ctx, -2);											// [obj][table][value]
		if (duk_is_null_or_undefined(ctx, -1)) { return(0); }
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Options);	//..[table][value][options]
		if (duk_has_prop_string(ctx, 1, "readset")) { duk_push_false(ctx); duk_put_prop_string(ctx, -2, "readset"); }
		if (duk_has_prop_string(ctx, 1, "writeset")) { duk_push_false(ctx); duk_put_prop_string(ctx, -2, "writeset"); }
		if (duk_has_prop_string(ctx, 1, "errorset")) { duk_push_false(ctx); duk_put_prop_string(ctx, -2, "errorset"); }
		if(	Duktape_GetBooleanProperty(ctx, -1, "readset", 0)	== 0 && 
			Duktape_GetBooleanProperty(ctx, -1, "writeset", 0)	== 0 &&
			Duktape_GetBooleanProperty(ctx, -1, "errorset", 0)	== 0)
		{
			// No FD_SET watchers, so we can remove the entire object
			duk_pop_2(ctx);												// [obj][table]
			duk_dup(ctx, 0);											// [obj][table][key]
			duk_del_prop(ctx, -2);										// [obj][table]
		}
	}
	else
	{
		// Remove All FD_SET watchers for this FD
		duk_del_prop(ctx, -2);											// [obj][table]
	}
	return(0);
}
#ifdef WIN32
char *DescriptorEvents_Status[] = { "NONE", "INVALID_HANDLE", "TIMEOUT", "REMOVED", "EXITING", "ERROR" }; 
BOOL ILibDuktape_DescriptorEvents_WaitHandleSink(void *chain, HANDLE h, ILibWaitHandle_ErrorStatus status, void* user)
{
	BOOL ret = FALSE;
	duk_context *ctx = (duk_context*)((void**)user)[0];

	int top = duk_get_top(ctx);
	duk_push_heapptr(ctx, ((void**)user)[1]);								// [events]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_HTable);		// [events][table]
	duk_push_sprintf(ctx, "%p", h);											// [events][table][key]
	duk_get_prop(ctx, -2);													// [events][table][val]
	if (!duk_is_null_or_undefined(ctx, -1))
	{
		void *hptr = duk_get_heapptr(ctx, -1);
		if (status != ILibWaitHandle_ErrorStatus_NONE) { duk_push_sprintf(ctx, "%p", h); duk_del_prop(ctx, -3); }
		duk_push_pointer(ctx, h); duk_put_prop_string(ctx, -3, ILibDuktape_DescriptorEvents_CURRENT);
		ILibDuktape_EventEmitter_SetupEmit(ctx, hptr, "signaled");			// [events][table][val][emit][this][signaled]
		duk_push_string(ctx, DescriptorEvents_Status[(int)status]);			// [events][table][val][emit][this][signaled][status]
		if (duk_pcall_method(ctx, 2) == 0)									// [events][table][val][undef]
		{
			ILibDuktape_EventEmitter_GetEmitReturn(ctx, hptr, "signaled");	// [events][table][val][undef][ret]
			if (duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) != 0)
			{
				ret = TRUE;
			}
		}	
		else
		{
			ILibDuktape_Process_UncaughtExceptionEx(ctx, "DescriptorEvents.signaled() threw an exception that will result in descriptor getting removed: ");
		}
		duk_set_top(ctx, top);
		duk_push_heapptr(ctx, ((void**)user)[1]);							// [events]
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_HTable);	// [events][table]

		if (ret == FALSE && Duktape_GetPointerProperty(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT) == h)
		{
			//
			// We need to unhook the events to the descriptor event object, before we remove it from the table
			//
			duk_push_sprintf(ctx, "%p", h);									// [events][table][key]
			duk_get_prop(ctx, -2);											// [events][table][descriptorevent]
			duk_get_prop_string(ctx, -1, "removeAllListeners");				// [events][table][descriptorevent][remove]
			duk_swap_top(ctx, -2);											// [events][table][remove][this]
			duk_push_string(ctx, "signaled");								// [events][table][remove][this][signaled]
			duk_pcall_method(ctx, 1); duk_pop(ctx);							// [events][table]
			duk_push_sprintf(ctx, "%p", h);									// [events][table][key]
			duk_del_prop(ctx, -2);											// [events][table]
		}
		duk_del_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT);	// [events][table]
	}
	duk_set_top(ctx, top);

	return(ret);
}
#endif
duk_ret_t ILibDuktape_DescriptorEvents_Add(duk_context *ctx)
{
	ILibDuktape_EventEmitter *e;
#ifdef WIN32
	if (duk_is_object(ctx, 0) && duk_has_prop_string(ctx, 0, "_ptr"))
	{
		// Adding a Windows Wait Handle
		HANDLE h = (HANDLE)Duktape_GetPointerProperty(ctx, 0, "_ptr");
		if (h != NULL)
		{
			// Normal Add Wait Handle
			char *metadata = "DescriptorEvents";
			int timeout = -1;
			duk_push_this(ctx);														// [events]
			ILibChain_Link *link = (ILibChain_Link*)Duktape_GetPointerProperty(ctx, -1, ILibDuktape_DescriptorEvents_ChainLink);
			duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_HTable);		// [events][table]
			if (Duktape_GetPointerProperty(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT) == h)
			{
				// We are adding a wait handle from the event handler for this same signal, so remove this attribute,
				// so the signaler doesn't remove the object we are about to put in.
				duk_del_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_CURRENT);
			}
			duk_push_object(ctx);													// [events][table][value]
			duk_push_sprintf(ctx, "%p", h);											// [events][table][value][key]
			duk_dup(ctx, -2);														// [events][table][value][key][value]
			duk_dup(ctx, 0);
			duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_WaitHandle);	// [events][table][value][key][value]
			if (duk_is_object(ctx, 1)) { duk_dup(ctx, 1); }
			else { duk_push_object(ctx); }											// [events][table][value][key][value][options]
			if (duk_has_prop_string(ctx, -1, "metadata"))
			{
				duk_push_string(ctx, "DescriptorEvents, ");							// [events][table][value][key][value][options][str1]
				duk_get_prop_string(ctx, -2, "metadata");							// [events][table][value][key][value][options][str1][str2]
				duk_string_concat(ctx, -2);											// [events][table][value][key][value][options][str1][newstr]
				duk_remove(ctx, -2);												// [events][table][value][key][value][options][newstr]
				metadata = (char*)duk_get_string(ctx, -1);
				duk_put_prop_string(ctx, -2, "metadata");							// [events][table][value][key][value][options]
			}
			timeout = Duktape_GetIntPropertyValue(ctx, -1, "timeout", -1);
			duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_Options);		// [events][table][value][key][value]
			duk_put_prop(ctx, -4);													// [events][table][value]
			e = ILibDuktape_EventEmitter_Create(ctx);
			ILibDuktape_EventEmitter_CreateEventEx(e, "signaled");
			ILibChain_AddWaitHandleEx(duk_ctx_chain(ctx), h, timeout, ILibDuktape_DescriptorEvents_WaitHandleSink, link->ExtraMemoryPtr, metadata);
			return(1);
		}
		return(ILibDuktape_Error(ctx, "Invalid Parameter"));
	}
#endif

	if (!duk_is_number(ctx, 0)) { return(ILibDuktape_Error(ctx, "Invalid Descriptor")); }
	ILibForceUnBlockChain(Duktape_GetChain(ctx));

	duk_push_this(ctx);													// [obj]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);	// [obj][table]
	duk_dup(ctx, 0);													// [obj][table][key]
	if (duk_has_prop(ctx, -2))											// [obj][table]
	{
		// There's already a watcher, so let's just merge the FD_SETS
		duk_dup(ctx, 0);												// [obj][table][key]
		duk_get_prop(ctx, -2);											// [obj][table][value]
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Options);	//..[table][value][options]
		if (Duktape_GetBooleanProperty(ctx, 1, "readset", 0) != 0) { duk_push_true(ctx); duk_put_prop_string(ctx, -2, "readset"); }
		if (Duktape_GetBooleanProperty(ctx, 1, "writeset", 0) != 0) { duk_push_true(ctx); duk_put_prop_string(ctx, -2, "writeset"); }
		if (Duktape_GetBooleanProperty(ctx, 1, "errorset", 0) != 0) { duk_push_true(ctx); duk_put_prop_string(ctx, -2, "errorset"); }
		duk_pop(ctx);													// [obj][table][value]
		return(1);
	}

	duk_push_object(ctx);												// [obj][table][value]
	duk_dup(ctx, 0);													// [obj][table][value][key]
	duk_dup(ctx, -2);													// [obj][table][value][key][value]
	e = ILibDuktape_EventEmitter_Create(ctx);	
	ILibDuktape_EventEmitter_CreateEventEx(e, "readset");
	ILibDuktape_EventEmitter_CreateEventEx(e, "writeset");
	ILibDuktape_EventEmitter_CreateEventEx(e, "errorset");
	duk_dup(ctx, 0);													// [obj][table][value][key][value][FD]
	duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_FD);		// [obj][table][value][key][value]
	duk_dup(ctx, 1);													// [obj][table][value][key][value][options]
	duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_Options);	// [obj][table][value][key][value]
	char* metadata = Duktape_GetStringPropertyValue(ctx, -1, "metadata", NULL);
	if (metadata != NULL)
	{
		duk_push_string(ctx, "DescriptorEvents, ");						// [obj][table][value][key][value][str1]
		duk_push_string(ctx, metadata);									// [obj][table][value][key][value][str1][str2]
		duk_string_concat(ctx, -2);										// [obj][table][value][key][value][newStr]
		duk_put_prop_string(ctx, -2, "metadata");						// [obj][table][value][key][value]
	}
	duk_put_prop(ctx, -4);												// [obj][table][value]

	return(1);
}
duk_ret_t ILibDuktape_DescriptorEvents_Finalizer(duk_context *ctx)
{
	ILibChain_Link *link = (ILibChain_Link*)Duktape_GetPointerProperty(ctx, 0, ILibDuktape_DescriptorEvents_ChainLink);
	void *chain = Duktape_GetChain(ctx);

	link->PreSelectHandler = NULL;
	link->PostSelectHandler = NULL;
	((void**)link->ExtraMemoryPtr)[0] = NULL;
	((void**)link->ExtraMemoryPtr)[1] = NULL;
	
	if (ILibIsChainBeingDestroyed(chain) == 0)
	{
		ILibChain_SafeRemove(chain, link);
	}

	return(0);
}

#ifndef WIN32
void ILibDuktape_DescriptorEvents_GetCount_results_final(void *chain, void *user)
{
	duk_context *ctx = (duk_context*)((void**)user)[0];
	void *hptr = ((void**)user)[1];
	duk_push_heapptr(ctx, hptr);											// [promise]
	duk_get_prop_string(ctx, -1, "_RES");									// [promise][res]
	duk_swap_top(ctx, -2);													// [res][this]
	duk_push_int(ctx, ILibChain_GetDescriptorCount(duk_ctx_chain(ctx)));	// [res][this][count]
	duk_pcall_method(ctx, 1); duk_pop(ctx);									// ...
	free(user);
}
void ILibDuktape_DescriptorEvents_GetCount_results(void *chain, void *user)
{
	ILibChain_RunOnMicrostackThreadEx2(chain, ILibDuktape_DescriptorEvents_GetCount_results_final, user, 1);
}
#endif
duk_ret_t ILibDuktape_DescriptorEvents_GetCount_promise(duk_context *ctx)
{
	duk_push_this(ctx);		// [promise]
	duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "_RES");
	duk_dup(ctx, 1); duk_put_prop_string(ctx, -2, "_REJ");
	return(0);
}
duk_ret_t ILibDuktape_DescriptorEvents_GetCount(duk_context *ctx)
{
	duk_eval_string(ctx, "require('promise');");								// [promise]
	duk_push_c_function(ctx, ILibDuktape_DescriptorEvents_GetCount_promise, 2);	// [promise][func]
	duk_new(ctx, 1);															// [promise]
	
#ifdef WIN32
	duk_get_prop_string(ctx, -1, "_RES");										// [promise][res]
	duk_dup(ctx, -2);															// [promise][res][this]
	duk_push_int(ctx, ILibChain_GetDescriptorCount(duk_ctx_chain(ctx)));		// [promise][res][this][count]
	duk_call_method(ctx, 1); duk_pop(ctx);										// [promise]
#else
	void **data = (void**)ILibMemory_Allocate(2 * sizeof(void*), 0, NULL, NULL);
	data[0] = ctx;
	data[1] = duk_get_heapptr(ctx, -1);
	ILibChain_InitDescriptorCount(duk_ctx_chain(ctx));
	ILibChain_RunOnMicrostackThreadEx2(duk_ctx_chain(ctx), ILibDuktape_DescriptorEvents_GetCount_results, data, 1);
#endif
	return(1);
}
char* ILibDuktape_DescriptorEvents_Query(void* chain, void *object, int fd, size_t *dataLen)
{
	char *retVal = ((ILibChain_Link*)object)->MetaData;
	*dataLen = strnlen_s(retVal, 1024);

	duk_context *ctx = (duk_context*)((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[0];
	void *h = ((void**)((ILibChain_Link*)object)->ExtraMemoryPtr)[1];
	if (h == NULL || ctx == NULL || !duk_ctx_is_alive(ctx)) { return(retVal); }
	int top = duk_get_top(ctx);

	duk_push_heapptr(ctx, h);												// [events]
	duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);		// [events][table]
	duk_push_int(ctx, fd);													// [events][table][key]
	if (duk_has_prop(ctx, -2) != 0)											// [events][table]
	{
		duk_push_int(ctx, fd); duk_get_prop(ctx, -2);						// [events][table][val]
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Options);	// [events][table][val][options]
		if (!duk_is_null_or_undefined(ctx, -1))
		{
			retVal = Duktape_GetStringPropertyValueEx(ctx, -1, "metadata", retVal, dataLen);
		}
	}

	duk_set_top(ctx, top);
	return(retVal);
}
duk_ret_t ILibDuktape_DescriptorEvents_descriptorAdded(duk_context *ctx)
{
	duk_push_this(ctx);																// [DescriptorEvents]
	if (duk_is_number(ctx, 0))
	{
		duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_Table);			// [DescriptorEvents][table]
		duk_dup(ctx, 0);															// [DescriptorEvents][table][key]
	}
	else
	{
		if (duk_is_object(ctx, 0) && duk_has_prop_string(ctx, 0, "_ptr"))
		{
			duk_get_prop_string(ctx, -1, ILibDuktape_DescriptorEvents_HTable);		// [DescriptorEvents][table]	
			duk_push_sprintf(ctx, "%p", Duktape_GetPointerProperty(ctx, 0, "_ptr"));// [DescriptorEvents][table][key]
		}
		else
		{
			return(ILibDuktape_Error(ctx, "Invalid Argument. Must be a descriptor or HANDLE"));
		}
	}
	duk_push_boolean(ctx, duk_has_prop(ctx, -2));
	return(1);
}
void ILibDuktape_DescriptorEvents_Push(duk_context *ctx, void *chain)
{
	ILibChain_Link *link = (ILibChain_Link*)ILibChain_Link_Allocate(sizeof(ILibChain_Link), 2 * sizeof(void*));
	link->MetaData = "DescriptorEvents";
	link->PreSelectHandler = ILibDuktape_DescriptorEvents_PreSelect;
	link->PostSelectHandler = ILibDuktape_DescriptorEvents_PostSelect;
	link->QueryHandler = ILibDuktape_DescriptorEvents_Query;

	duk_push_object(ctx);
	duk_push_pointer(ctx, link); duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_ChainLink);
	duk_push_object(ctx); duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_Table);
	duk_push_object(ctx); duk_put_prop_string(ctx, -2, ILibDuktape_DescriptorEvents_HTable);
	
	ILibDuktape_CreateFinalizer(ctx, ILibDuktape_DescriptorEvents_Finalizer);

	((void**)link->ExtraMemoryPtr)[0] = ctx;
	((void**)link->ExtraMemoryPtr)[1] = duk_get_heapptr(ctx, -1);
	ILibDuktape_CreateInstanceMethod(ctx, "addDescriptor", ILibDuktape_DescriptorEvents_Add, 2);
	ILibDuktape_CreateInstanceMethod(ctx, "removeDescriptor", ILibDuktape_DescriptorEvents_Remove, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "getDescriptorCount", ILibDuktape_DescriptorEvents_GetCount, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "descriptorAdded", ILibDuktape_DescriptorEvents_descriptorAdded, 1);

	ILibAddToChain(chain, link);
}
duk_ret_t ILibDuktape_Polyfills_filehash(duk_context *ctx)
{
	char *hash = duk_push_fixed_buffer(ctx, UTIL_SHA384_HASHSIZE);
	duk_push_buffer_object(ctx, -1, 0, UTIL_SHA384_HASHSIZE, DUK_BUFOBJ_NODEJS_BUFFER);
	if (GenerateSHA384FileHash((char*)duk_require_string(ctx, 0), hash) == 0)
	{
		return(1);
	}
	else
	{
		return(ILibDuktape_Error(ctx, "Error generating FileHash "));
	}
}

duk_ret_t ILibDuktape_Polyfills_ipv4From(duk_context *ctx)
{
	int v = duk_require_int(ctx, 0);
	ILibDuktape_IPV4AddressToOptions(ctx, v);
	duk_get_prop_string(ctx, -1, "host");
	return(1);
}

duk_ret_t ILibDuktape_Polyfills_global(duk_context *ctx)
{
	duk_push_global_object(ctx);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_isBuffer(duk_context *ctx)
{
	duk_push_boolean(ctx, duk_is_buffer_data(ctx, 0));
	return(1);
}
#if defined(_POSIX) && !defined(__APPLE__) && !defined(_FREEBSD)
duk_ret_t ILibDuktape_ioctl_func(duk_context *ctx)
{
	int fd = (int)duk_require_int(ctx, 0);
	int code = (int)duk_require_int(ctx, 1);
	duk_size_t outBufferLen = 0;
	char *outBuffer = Duktape_GetBuffer(ctx, 2, &outBufferLen);

	duk_push_int(ctx, ioctl(fd, _IOC(_IOC_READ | _IOC_WRITE, 'H', code, outBufferLen), outBuffer) ? errno : 0);
	return(1);
}
void ILibDuktape_ioctl_Push(duk_context *ctx, void *chain)
{
	duk_push_c_function(ctx, ILibDuktape_ioctl_func, DUK_VARARGS);
	ILibDuktape_WriteID(ctx, "ioctl");
}
#endif
void ILibDuktape_uuidv4_Push(duk_context *ctx, void *chain)
{	
	duk_push_object(ctx);
	char uuid[] = "module.exports = function uuidv4()\
						{\
							var b = Buffer.alloc(16);\
							b.randomFill();\
							var v = b.readUInt16BE(6) & 0xF1F;\
							v |= (4 << 12);\
							v |= (4 << 5);\
							b.writeUInt16BE(v, 6);\
							var ret = b.slice(0, 4).toString('hex') + '-' + b.slice(4, 6).toString('hex') + '-' + b.slice(6, 8).toString('hex') + '-' + b.slice(8, 10).toString('hex') + '-' + b.slice(10).toString('hex');\
							ret = '{' + ret.toLowerCase() + '}';\
							return (ret);\
						};";

	ILibDuktape_ModSearch_AddHandler_AlsoIncludeJS(ctx, uuid, sizeof(uuid) - 1);
}

duk_ret_t ILibDuktape_Polyfills_debugHang(duk_context *ctx)
{
	int val = duk_get_top(ctx) == 0 ? 30000 : duk_require_int(ctx, 0);

#ifdef WIN32
	Sleep(val);
#else
	sleep(val);
#endif

	return(0);
}

extern void checkForEmbeddedMSH_ex2(char *binPath, char **eMSH);
duk_ret_t ILibDuktape_Polyfills_MSH(duk_context *ctx)
{
	duk_eval_string(ctx, "process.execPath");	// [string]
	char *exepath = (char*)duk_get_string(ctx, -1);
	char *msh;
	duk_size_t s = 0;

	checkForEmbeddedMSH_ex2(exepath, &msh);
	if (msh == NULL)
	{
		duk_eval_string(ctx, "require('fs')");			// [fs]
		duk_get_prop_string(ctx, -1, "readFileSync");	// [fs][readFileSync]
		duk_swap_top(ctx, -2);							// [readFileSync][this]
#ifdef _POSIX
		duk_push_sprintf(ctx, "%s.msh", exepath);		// [readFileSync][this][path]
#else
		duk_push_string(ctx, exepath);					// [readFileSync][this][path]
		duk_string_split(ctx, -1, ".exe");				// [readFileSync][this][path][array]
		duk_remove(ctx, -2);							// [readFileSync][this][array]
		duk_array_join(ctx, -1, ".msh");				// [readFileSync][this][array][path]
		duk_remove(ctx, -2);							// [readFileSync][this][path]
#endif
		duk_push_object(ctx);							// [readFileSync][this][path][options]
		duk_push_string(ctx, "rb"); duk_put_prop_string(ctx, -2, "flags");
		if (duk_pcall_method(ctx, 2) == 0)				// [buffer]
		{
			msh = Duktape_GetBuffer(ctx, -1, &s);
		}
	}

	duk_push_object(ctx);														// [obj]
	if (msh != NULL)
	{
		if (s == 0) { s = ILibMemory_Size(msh); }
		parser_result *pr = ILibParseString(msh, 0, s, "\n", 1);
		parser_result_field *f = pr->FirstResult;
		int i;
		while (f != NULL)
		{
			if (f->datalength > 0)
			{
				i = ILibString_IndexOf(f->data, f->datalength, "=", 1);
				if (i >= 0)
				{
					duk_push_lstring(ctx, f->data, (duk_size_t)i);						// [obj][key]
					if (f->data[f->datalength - 1] == '\r')
					{
						duk_push_lstring(ctx, f->data + i + 1, f->datalength - i - 2);	// [obj][key][value]
					}
					else
					{
						duk_push_lstring(ctx, f->data + i + 1, f->datalength - i - 1);	// [obj][key][value]
					}
					duk_put_prop(ctx, -3);												// [obj]
				}
			}
			f = f->NextResult;
		}
		ILibDestructParserResults(pr);
		ILibMemory_Free(msh);
	}																					// [msh]

	if (duk_peval_string(ctx, "require('MeshAgent').getStartupOptions()") == 0)			// [msh][obj]
	{
		duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);								// [msh][obj][enum]
		while (duk_next(ctx, -1, 1))													// [msh][obj][enum][key][val]
		{
			if (duk_has_prop_string(ctx, -5, duk_get_string(ctx, -2)) == 0)
			{
				duk_put_prop(ctx, -5);													// [msh][obj][enum]
			}
			else
			{
				duk_pop_2(ctx);															// [msh][obj][enum]
			}
		}
		duk_pop(ctx);																	// [msh][obj]
	}
	duk_pop(ctx);																		// [msh]
	return(1);
}
#if defined(ILIBMEMTRACK) && !defined(ILIBCHAIN_GLOBAL_LOCK)
extern size_t ILib_NativeAllocSize;
extern ILibSpinLock ILib_MemoryTrackLock;
duk_ret_t ILibDuktape_Polyfills_NativeAllocSize(duk_context *ctx)
{
	ILibSpinLock_Lock(&ILib_MemoryTrackLock);
	duk_push_uint(ctx, ILib_NativeAllocSize);
	ILibSpinLock_UnLock(&ILib_MemoryTrackLock);
	return(1);
}
#endif
duk_ret_t ILibDuktape_Polyfills_WeakReference_isAlive(duk_context *ctx)
{
	duk_push_this(ctx);								// [weak]
	void **p = Duktape_GetPointerProperty(ctx, -1, "\xFF_heapptr");
	duk_push_boolean(ctx, ILibMemory_CanaryOK(p));
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_WeakReference_object(duk_context *ctx)
{
	duk_push_this(ctx);								// [weak]
	void **p = Duktape_GetPointerProperty(ctx, -1, "\xFF_heapptr");
	if (ILibMemory_CanaryOK(p))
	{
		duk_push_heapptr(ctx, p[0]);
	}
	else
	{
		duk_push_null(ctx);
	}
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_WeakReference(duk_context *ctx)
{
	duk_push_object(ctx);														// [weak]
	ILibDuktape_WriteID(ctx, "WeakReference");		
	duk_dup(ctx, 0);															// [weak][obj]
	void *j = duk_get_heapptr(ctx, -1);
	void **p = (void**)Duktape_PushBuffer(ctx, sizeof(void*));					// [weak][obj][buffer]
	p[0] = j;
	duk_put_prop_string(ctx, -2, Duktape_GetStashKey(duk_get_heapptr(ctx, -1)));// [weak][obj]

	duk_pop(ctx);																// [weak]

	duk_push_pointer(ctx, p); duk_put_prop_string(ctx, -2, "\xFF_heapptr");		// [weak]
	ILibDuktape_CreateInstanceMethod(ctx, "isAlive", ILibDuktape_Polyfills_WeakReference_isAlive, 0);
	ILibDuktape_CreateEventWithGetter_SetEnumerable(ctx, "object", ILibDuktape_Polyfills_WeakReference_object, 1);
	return(1);
}

duk_ret_t ILibDuktape_Polyfills_rootObject(duk_context *ctx)
{
	void *h = _duk_get_first_object(ctx);
	duk_push_heapptr(ctx, h);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_nextObject(duk_context *ctx)
{
	void *h = duk_require_heapptr(ctx, 0);
	void *next = _duk_get_next_object(ctx, h);
	if (next != NULL)
	{
		duk_push_heapptr(ctx, next);
	}
	else
	{
		duk_push_null(ctx);
	}
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_countObject(duk_context *ctx)
{
	void *h = _duk_get_first_object(ctx);
	duk_int_t i = 1;

	while (h != NULL)
	{
		if ((h = _duk_get_next_object(ctx, h)) != NULL) { ++i; }
	}
	duk_push_int(ctx, i);
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_hide(duk_context *ctx)
{
	duk_idx_t top = duk_get_top(ctx);
	duk_push_heap_stash(ctx);									// [stash]

	if (top == 0)
	{
		duk_get_prop_string(ctx, -1, "__STASH__");				// [stash][value]
	}
	else
	{
		if (duk_is_boolean(ctx, 0))
		{
			duk_get_prop_string(ctx, -1, "__STASH__");			// [stash][value]
			if (duk_require_boolean(ctx, 0))
			{
				duk_del_prop_string(ctx, -2, "__STASH__");
			}
		}
		else
		{
			duk_dup(ctx, 0);									// [stash][value]
			duk_dup(ctx, -1);									// [stash][value][value]
			duk_put_prop_string(ctx, -3, "__STASH__");			// [stash][value]
		}
	}
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_altrequire(duk_context *ctx)
{
	duk_size_t idLen;
	char *id = (char*)duk_get_lstring(ctx, 0, &idLen);

	duk_push_heap_stash(ctx);										// [stash]
	if (!duk_has_prop_string(ctx, -1, ILibDuktape_AltRequireTable))
	{
		duk_push_object(ctx); 
		duk_put_prop_string(ctx, -2, ILibDuktape_AltRequireTable);
	}
	duk_get_prop_string(ctx, -1, ILibDuktape_AltRequireTable);		// [stash][table]

	if (ILibDuktape_ModSearch_IsRequired(ctx, id, idLen) == 0)
	{
		// Module was not 'require'ed yet
		duk_push_sprintf(ctx, "global._legacyrequire('%s');", id);	// [stash][table][str]
		duk_eval(ctx);												// [stash][table][value]
		duk_dup(ctx, -1);											// [stash][table][value][value]
		duk_put_prop_string(ctx, -3, id);							// [stash][table][value]
	}
	else
	{
		// Module was already required, so we need to do some additional checks
		if (duk_has_prop_string(ctx, -1, id)) // Check to see if there is a new instance we can use
		{
			duk_get_prop_string(ctx, -1, id);							// [stash][table][value]
		}
		else
		{
			// There is not an instance here, so we need to instantiate a new alt instance
			duk_push_sprintf(ctx, "getJSModule('%s');", id);			// [stash][table][str]
			if (duk_peval(ctx) != 0)									// [stash][table][js]
			{
				// This was a native module, so just return it directly
				duk_push_sprintf(ctx, "global._legacyrequire('%s');", id);	
				duk_eval(ctx);												
				return(1);
			}
			duk_eval_string(ctx, "global._legacyrequire('uuid/v4')();");				// [stash][table][js][uuid]
			duk_push_sprintf(ctx, "%s_%s", id, duk_get_string(ctx, -1));// [stash][table][js][uuid][newkey]

			duk_push_global_object(ctx);				// [stash][table][js][uuid][newkey][g]
			duk_get_prop_string(ctx, -1, "addModule");	// [stash][table][js][uuid][newkey][g][addmodule]
			duk_remove(ctx, -2);						// [stash][table][js][uuid][newkey][addmodule]
			duk_dup(ctx, -2);							// [stash][table][js][uuid][newkey][addmodule][key]
			duk_dup(ctx, -5);							// [stash][table][js][uuid][newkey][addmodule][key][module]
			duk_call(ctx, 2);							// [stash][table][js][uuid][newkey][ret]
			duk_pop(ctx);								// [stash][table][js][uuid][newkey]
			duk_push_sprintf(ctx, "global._legacyrequire('%s');", duk_get_string(ctx, -1));
			duk_eval(ctx);								// [stash][table][js][uuid][newkey][newval]
			duk_dup(ctx, -1);							// [stash][table][js][uuid][newkey][newval][newval]
			duk_put_prop_string(ctx, -6, id);			// [stash][table][js][uuid][newkey][newval]
		}
	}
	return(1);
}
duk_ret_t ILibDuktape_Polyfills_resolve(duk_context *ctx)
{
	char tmp[512];
	char *host = (char*)duk_require_string(ctx, 0);
	struct sockaddr_in6 addr[16];
	memset(&addr, 0, sizeof(addr));

	int i, count = ILibResolveEx2(host, 443, addr, 16);
	duk_push_array(ctx);															// [ret]
	duk_push_array(ctx);															// [ret][integers]

	for (i = 0; i < count; ++i)
	{
		if (ILibInet_ntop2((struct sockaddr*)(&addr[i]), tmp, sizeof(tmp)) != NULL)
		{
			duk_push_string(ctx, tmp);												// [ret][integers][string]
			duk_array_push(ctx, -3);												// [ret][integers]

			duk_push_int(ctx, ((struct sockaddr_in*)&addr[i])->sin_addr.s_addr);	// [ret][integers][value]
			duk_array_push(ctx, -2);												// [ret][integers]
		}
	}
	ILibDuktape_CreateReadonlyProperty_SetEnumerable(ctx, "_integers", 0);			// [ret]
	return(1);
}
void ILibDuktape_Polyfills_Init(duk_context *ctx)
{
	ILibDuktape_ModSearch_AddHandler(ctx, "queue", ILibDuktape_Queue_Push);
	ILibDuktape_ModSearch_AddHandler(ctx, "DynamicBuffer", ILibDuktape_DynamicBuffer_Push);
	ILibDuktape_ModSearch_AddHandler(ctx, "stream", ILibDuktape_Stream_Init);
	ILibDuktape_ModSearch_AddHandler(ctx, "http-headers", ILibDuktape_httpHeaders_PUSH);

#ifndef MICROSTACK_NOTLS
	ILibDuktape_ModSearch_AddHandler(ctx, "pkcs7", ILibDuktape_PKCS7_Push);
#endif

#ifndef MICROSTACK_NOTLS
	ILibDuktape_ModSearch_AddHandler(ctx, "bignum", ILibDuktape_bignum_Push);
	ILibDuktape_ModSearch_AddHandler(ctx, "dataGenerator", ILibDuktape_dataGenerator_Push);
#endif
	ILibDuktape_ModSearch_AddHandler(ctx, "ChainViewer", ILibDuktape_ChainViewer_Push);
	ILibDuktape_ModSearch_AddHandler(ctx, "DescriptorEvents", ILibDuktape_DescriptorEvents_Push);
	ILibDuktape_ModSearch_AddHandler(ctx, "uuid/v4", ILibDuktape_uuidv4_Push);
#if defined(_POSIX) && !defined(__APPLE__) && !defined(_FREEBSD)
	ILibDuktape_ModSearch_AddHandler(ctx, "ioctl", ILibDuktape_ioctl_Push);
#endif


	// Global Polyfills
	duk_push_global_object(ctx);													// [g]
	ILibDuktape_WriteID(ctx, "Global");
	ILibDuktape_Polyfills_Array(ctx);
	ILibDuktape_Polyfills_String(ctx);
	ILibDuktape_Polyfills_Buffer(ctx);
	ILibDuktape_Polyfills_Console(ctx);
	ILibDuktape_Polyfills_byte_ordering(ctx);
	ILibDuktape_Polyfills_timer(ctx);
	ILibDuktape_Polyfills_object(ctx);
	ILibDuktape_Polyfills_function(ctx);
	
	ILibDuktape_CreateInstanceMethod(ctx, "addModuleObject", ILibDuktape_Polyfills_addModuleObject, 2);
	ILibDuktape_CreateInstanceMethod(ctx, "addModule", ILibDuktape_Polyfills_addModule, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "addCompressedModule", ILibDuktape_Polyfills_addCompressedModule, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "getJSModule", ILibDuktape_Polyfills_getJSModule, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "getJSModuleDate", ILibDuktape_Polyfills_getJSModuleDate, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_debugHang", ILibDuktape_Polyfills_debugHang, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "_debugCrash", ILibDuktape_Polyfills_debugCrash, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "_debugGC", ILibDuktape_Polyfills_debugGC, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "_debug", ILibDuktape_Polyfills_debug, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "getSHA384FileHash", ILibDuktape_Polyfills_filehash, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_ipv4From", ILibDuktape_Polyfills_ipv4From, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_isBuffer", ILibDuktape_Polyfills_isBuffer, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_MSH", ILibDuktape_Polyfills_MSH, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "WeakReference", ILibDuktape_Polyfills_WeakReference, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_rootObject", ILibDuktape_Polyfills_rootObject, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "_nextObject", ILibDuktape_Polyfills_nextObject, 1);
	ILibDuktape_CreateInstanceMethod(ctx, "_countObjects", ILibDuktape_Polyfills_countObject, 0);
	ILibDuktape_CreateInstanceMethod(ctx, "_hide", ILibDuktape_Polyfills_hide, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "_altrequire", ILibDuktape_Polyfills_altrequire, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "resolve", ILibDuktape_Polyfills_resolve, 1);

#if defined(ILIBMEMTRACK) && !defined(ILIBCHAIN_GLOBAL_LOCK)
	ILibDuktape_CreateInstanceMethod(ctx, "_NativeAllocSize", ILibDuktape_Polyfills_NativeAllocSize, 0);
#endif

#ifndef MICROSTACK_NOTLS
	ILibDuktape_CreateInstanceMethod(ctx, "crc32c", ILibDuktape_Polyfills_crc32c, DUK_VARARGS);
	ILibDuktape_CreateInstanceMethod(ctx, "crc32", ILibDuktape_Polyfills_crc32, DUK_VARARGS);
#endif
	ILibDuktape_CreateEventWithGetter(ctx, "global", ILibDuktape_Polyfills_global);
	duk_pop(ctx);																	// ...

	ILibDuktape_Debugger_Init(ctx, 9091);
}

#ifdef __DOXY__
/*!
\brief String 
*/
class String
{
public:
	/*!
	\brief Finds a String within another String
	\param str \<String\> Substring to search for
	\return <Integer> Index of where the string was found. -1 if not found
	*/
	Integer indexOf(str);
	/*!
	\brief Extracts a String from a String.
	\param startIndex <Integer> Starting index to extract
	\param length <Integer> Number of characters to extract
	\return \<String\> extracted String
	*/
	String substr(startIndex, length);
	/*!
	\brief Extracts a String from a String.
	\param startIndex <Integer> Starting index to extract
	\param endIndex <Integer> Ending index to extract
	\return \<String\> extracted String
	*/
	String splice(startIndex, endIndex);
	/*!
	\brief Split String into substrings
	\param str \<String\> Delimiter to split on
	\return Array of Tokens
	*/
	Array<String> split(str);
	/*!
	\brief Determines if a String starts with the given substring
	\param str \<String\> substring 
	\return <boolean> True, if this String starts with the given substring
	*/
	boolean startsWith(str);
};
/*!
\brief Instances of the Buffer class are similar to arrays of integers but correspond to fixed-sized, raw memory allocations.
*/
class Buffer
{
public:
	/*!
	\brief Create a new Buffer instance of the specified number of bytes
	\param size <integer> 
	\return \<Buffer\> new Buffer instance
	*/
	Buffer(size);

	/*!
	\brief Returns the amount of memory allocated in  bytes
	*/
	integer length;
	/*!
	\brief Creates a new Buffer instance from an encoded String
	\param str \<String\> encoded String
	\param encoding \<String\> Encoding. Can be either 'base64' or 'hex'
	\return \<Buffer\> new Buffer instance
	*/
	static Buffer from(str, encoding);
	/*!
	\brief Decodes Buffer to a String
	\param encoding \<String\> Optional. Can be either 'base64' or 'hex'. If not specified, will just encode as an ANSI string
	\param start <integer> Optional. Starting offset. <b>Default:</b> 0
	\param end <integer> Optional. Ending offset (not inclusive) <b>Default:</b> buffer length
	\return \<String\> Encoded String
	*/
	String toString([encoding[, start[, end]]]);
	/*!
	\brief Returns a new Buffer that references the same memory as the original, but offset and cropped by the start and end indices.
	\param start <integer> Where the new Buffer will start. <b>Default:</b> 0
	\param end <integer> Where the new Buffer will end. (Not inclusive) <b>Default:</b> buffer length
	\return \<Buffer\> 
	*/
	Buffer slice([start[, end]]);
};
/*!
\brief Console
*/
class Console
{
public:
	/*!
	\brief Serializes the input parameters to the Console Display
	\param args <any>
	*/
	void log(...args);
};
/*!
\brief Global Timer Methods
*/
class Timers
{
public:
	/*!
	\brief Schedules the "immediate" execution of the callback after I/O events' callbacks. 
	\param callback <func> Function to call at the end of the event loop
	\param args <any> Optional arguments to pass when the callback is called
	\return Immediate for use with clearImmediate().
	*/
	Immediate setImmediate(callback[, ...args]);
	/*!
	\brief Schedules execution of a one-time callback after delay milliseconds. 
	\param callback <func> Function to call when the timeout elapses
	\param args <any> Optional arguments to pass when the callback is called
	\return Timeout for use with clearTimeout().
	*/
	Timeout setTimeout(callback, delay[, ...args]);
	/*!
	\brief Schedules repeated execution of callback every delay milliseconds.
	\param callback <func> Function to call when the timer elapses
	\param args <any> Optional arguments to pass when the callback is called
	\return Timeout for use with clearInterval().
	*/
	Timeout setInterval(callback, delay[, ...args]);

	/*!
	\brief Cancels a Timeout returned by setTimeout()
	\param timeout Timeout
	*/
	void clearTimeout(timeout);
	/*!
	\brief Cancels a Timeout returned by setInterval()
	\param interval Timeout
	*/
	void clearInterval(interval);
	/*!
	\brief Cancels an Immediate returned by setImmediate()
	\param immediate Immediate
	*/
	void clearImmediate(immediate);

	/*!
	\brief Scheduled Timer
	*/
	class Timeout
	{
	public:
	};
	/*!
	\implements Timeout
	\brief Scheduled Immediate
	*/
	class Immediate
	{
	public:
	};
};

/*!
\brief Global methods for byte ordering manipulation
*/
class BytesOrdering
{
public:
	/*!
	\brief Converts 2 bytes from network order to host order
	\param buffer \<Buffer\> bytes to convert
	\param offset <integer> offset to start
	\return <integer> host order value
	*/
	static integer ntohs(buffer, offset);
	/*!
	\brief Converts 4 bytes from network order to host order
	\param buffer \<Buffer\> bytes to convert
	\param offset <integer> offset to start
	\return <integer> host order value
	*/
	static integer ntohl(buffer, offset);
	/*!
	\brief Writes 2 bytes in network order
	\param buffer \<Buffer\> Buffer to write to
	\param offset <integer> offset to start writing
	\param val <integer> host order value to write
	*/
	static void htons(buffer, offset, val);
	/*!
	\brief Writes 4 bytes in network order
	\param buffer \<Buffer\> Buffer to write to
	\param offset <integer> offset to start writing
	\param val <integer> host order value to write
	*/
	static void htonl(buffer, offset, val);
};
#endif
