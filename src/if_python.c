/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */
/*
 * Python extensions by Paul Moore.
 * Changes for Unix by David Leonard.
 *
 * This consists of four parts:
 * 1. Python interpreter main program
 * 2. Python output stream: writes output via [e]msg().
 * 3. Implementation of the Vim module for Python
 * 4. Utility functions for handling the interface between Vim and Python.
 */

#include "vim.h"

#include <limits.h>

/* Python.h defines _POSIX_THREADS itself (if needed) */
#ifdef _POSIX_THREADS
# undef _POSIX_THREADS
#endif

#if defined(_WIN32) && defined(HAVE_FCNTL_H)
# undef HAVE_FCNTL_H
#endif

#ifdef _DEBUG
# undef _DEBUG
#endif

#ifdef HAVE_STDARG_H
# undef HAVE_STDARG_H	/* Python's config.h defines it as well. */
#endif
#ifdef _POSIX_C_SOURCE
# undef _POSIX_C_SOURCE	/* pyconfig.h defines it as well. */
#endif
#ifdef _XOPEN_SOURCE
# undef _XOPEN_SOURCE	/* pyconfig.h defines it as well. */
#endif

#define PY_SSIZE_T_CLEAN

#include <Python.h>
#if defined(MACOS) && !defined(MACOS_X_UNIX)
# include "macglue.h"
# include <CodeFragments.h>
#endif
#undef main /* Defined in python.h - aargh */
#undef HAVE_FCNTL_H /* Clash with os_win32.h */

static void init_structs(void);

/* No-op conversion functions, use with care! */
#define PyString_AsBytes(obj) (obj)
#define PyString_FreeBytes(obj)

#if !defined(FEAT_PYTHON) && defined(PROTO)
/* Use this to be able to generate prototypes without python being used. */
# define PyObject Py_ssize_t
# define PyThreadState Py_ssize_t
# define PyTypeObject Py_ssize_t
struct PyMethodDef { Py_ssize_t a; };
# define PySequenceMethods Py_ssize_t
#endif

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02050000
# define PyInt Py_ssize_t
# define PyInquiry lenfunc
# define PyIntArgFunc ssizeargfunc
# define PyIntIntArgFunc ssizessizeargfunc
# define PyIntObjArgProc ssizeobjargproc
# define PyIntIntObjArgProc ssizessizeobjargproc
# define Py_ssize_t_fmt "n"
#else
# define PyInt int
# define PyInquiry inquiry
# define PyIntArgFunc intargfunc
# define PyIntIntArgFunc intintargfunc
# define PyIntObjArgProc intobjargproc
# define PyIntIntObjArgProc intintobjargproc
# define Py_ssize_t_fmt "i"
#endif

/* Parser flags */
#define single_input	256
#define file_input	257
#define eval_input	258

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x020300F0
  /* Python 2.3: can invoke ":python" recursively. */
# define PY_CAN_RECURSE
#endif

# if defined(DYNAMIC_PYTHON) || defined(PROTO)
#  ifndef DYNAMIC_PYTHON
#   define HINSTANCE long_u		/* for generating prototypes */
#  endif

# ifndef WIN3264
#  include <dlfcn.h>
#  define FARPROC void*
#  define HINSTANCE void*
#  if defined(PY_NO_RTLD_GLOBAL) && defined(PY3_NO_RTLD_GLOBAL)
#   define load_dll(n) dlopen((n), RTLD_LAZY)
#  else
#   define load_dll(n) dlopen((n), RTLD_LAZY|RTLD_GLOBAL)
#  endif
#  define close_dll dlclose
#  define symbol_from_dll dlsym
# else
#  define load_dll vimLoadLib
#  define close_dll FreeLibrary
#  define symbol_from_dll GetProcAddress
# endif

/* This makes if_python.c compile without warnings against Python 2.5
 * on Win32 and Win64. */
# undef PyRun_SimpleString
# undef PyRun_String
# undef PyArg_Parse
# undef PyArg_ParseTuple
# undef Py_BuildValue
# undef Py_InitModule4
# undef Py_InitModule4_64

/*
 * Wrapper defines
 */
# define PyArg_Parse dll_PyArg_Parse
# define PyArg_ParseTuple dll_PyArg_ParseTuple
# define PyMem_Free dll_PyMem_Free
# define PyDict_SetItemString dll_PyDict_SetItemString
# define PyErr_BadArgument dll_PyErr_BadArgument
# define PyErr_Clear dll_PyErr_Clear
# define PyErr_NoMemory dll_PyErr_NoMemory
# define PyErr_Occurred dll_PyErr_Occurred
# define PyErr_SetNone dll_PyErr_SetNone
# define PyErr_SetString dll_PyErr_SetString
# define PyEval_InitThreads dll_PyEval_InitThreads
# define PyEval_RestoreThread dll_PyEval_RestoreThread
# define PyEval_SaveThread dll_PyEval_SaveThread
# ifdef PY_CAN_RECURSE
#  define PyGILState_Ensure dll_PyGILState_Ensure
#  define PyGILState_Release dll_PyGILState_Release
# endif
# define PyInt_AsLong dll_PyInt_AsLong
# define PyInt_FromLong dll_PyInt_FromLong
# define PyInt_Type (*dll_PyInt_Type)
# define PyList_GetItem dll_PyList_GetItem
# define PyList_Append dll_PyList_Append
# define PyList_New dll_PyList_New
# define PyList_SetItem dll_PyList_SetItem
# define PyList_Size dll_PyList_Size
# define PyList_Type (*dll_PyList_Type)
# define PyTuple_Size dll_PyTuple_Size
# define PyTuple_GetItem dll_PyTuple_GetItem
# define PyTuple_Type (*dll_PyTuple_Type)
# define PyImport_ImportModule dll_PyImport_ImportModule
# define PyDict_New dll_PyDict_New
# define PyDict_GetItemString dll_PyDict_GetItemString
# define PyDict_Items dll_PyDict_Items
# define PyModule_GetDict dll_PyModule_GetDict
# define PyRun_SimpleString dll_PyRun_SimpleString
# define PyRun_String dll_PyRun_String
# define PyString_AsString dll_PyString_AsString
# define PyString_FromString dll_PyString_FromString
# define PyString_FromStringAndSize dll_PyString_FromStringAndSize
# define PyString_Size dll_PyString_Size
# define PyString_Type (*dll_PyString_Type)
# define PyFloat_AsDouble dll_PyFloat_AsDouble
# define PyFloat_FromDouble dll_PyFloat_FromDouble
# define PyFloat_Type (*dll_PyFloat_Type)
# define PyImport_AddModule (*dll_PyImport_AddModule)
# define PySys_SetObject dll_PySys_SetObject
# define PySys_SetArgv dll_PySys_SetArgv
# define PyType_Type (*dll_PyType_Type)
# define PyType_Ready (*dll_PyType_Ready)
# define Py_BuildValue dll_Py_BuildValue
# define Py_FindMethod dll_Py_FindMethod
# define Py_InitModule4 dll_Py_InitModule4
# define Py_SetPythonHome dll_Py_SetPythonHome
# define Py_Initialize dll_Py_Initialize
# define Py_Finalize dll_Py_Finalize
# define Py_IsInitialized dll_Py_IsInitialized
# define _PyObject_New dll__PyObject_New
# define _Py_NoneStruct (*dll__Py_NoneStruct)
# define PyObject_Init dll__PyObject_Init
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
#  define PyType_IsSubtype dll_PyType_IsSubtype
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
#  define PyObject_Malloc dll_PyObject_Malloc
#  define PyObject_Free dll_PyObject_Free
# endif

/*
 * Pointers for dynamic link
 */
static int(*dll_PyArg_Parse)(PyObject *, char *, ...);
static int(*dll_PyArg_ParseTuple)(PyObject *, char *, ...);
static int(*dll_PyMem_Free)(void *);
static int(*dll_PyDict_SetItemString)(PyObject *dp, char *key, PyObject *item);
static int(*dll_PyErr_BadArgument)(void);
static void(*dll_PyErr_Clear)(void);
static PyObject*(*dll_PyErr_NoMemory)(void);
static PyObject*(*dll_PyErr_Occurred)(void);
static void(*dll_PyErr_SetNone)(PyObject *);
static void(*dll_PyErr_SetString)(PyObject *, const char *);
static void(*dll_PyEval_InitThreads)(void);
static void(*dll_PyEval_RestoreThread)(PyThreadState *);
static PyThreadState*(*dll_PyEval_SaveThread)(void);
# ifdef PY_CAN_RECURSE
static PyGILState_STATE	(*dll_PyGILState_Ensure)(void);
static void (*dll_PyGILState_Release)(PyGILState_STATE);
#endif
static long(*dll_PyInt_AsLong)(PyObject *);
static PyObject*(*dll_PyInt_FromLong)(long);
static PyTypeObject* dll_PyInt_Type;
static PyObject*(*dll_PyList_GetItem)(PyObject *, PyInt);
static PyObject*(*dll_PyList_Append)(PyObject *, PyObject *);
static PyObject*(*dll_PyList_New)(PyInt size);
static int(*dll_PyList_SetItem)(PyObject *, PyInt, PyObject *);
static PyInt(*dll_PyList_Size)(PyObject *);
static PyTypeObject* dll_PyList_Type;
static PyInt(*dll_PyTuple_Size)(PyObject *);
static PyObject*(*dll_PyTuple_GetItem)(PyObject *, PyInt);
static PyTypeObject* dll_PyTuple_Type;
static PyObject*(*dll_PyImport_ImportModule)(const char *);
static PyObject*(*dll_PyDict_New)(void);
static PyObject*(*dll_PyDict_GetItemString)(PyObject *, const char *);
static PyObject*(*dll_PyDict_Items)(PyObject *);
static PyObject*(*dll_PyModule_GetDict)(PyObject *);
static int(*dll_PyRun_SimpleString)(char *);
static PyObject *(*dll_PyRun_String)(char *, int, PyObject *, PyObject *);
static char*(*dll_PyString_AsString)(PyObject *);
static PyObject*(*dll_PyString_FromString)(const char *);
static PyObject*(*dll_PyString_FromStringAndSize)(const char *, PyInt);
static PyInt(*dll_PyString_Size)(PyObject *);
static PyTypeObject* dll_PyString_Type;
static double(*dll_PyFloat_AsDouble)(PyObject *);
static PyObject*(*dll_PyFloat_FromDouble)(double);
static PyTypeObject* dll_PyFloat_Type;
static int(*dll_PySys_SetObject)(char *, PyObject *);
static int(*dll_PySys_SetArgv)(int, char **);
static PyTypeObject* dll_PyType_Type;
static int (*dll_PyType_Ready)(PyTypeObject *type);
static PyObject*(*dll_Py_BuildValue)(char *, ...);
static PyObject*(*dll_Py_FindMethod)(struct PyMethodDef[], PyObject *, char *);
static PyObject*(*dll_Py_InitModule4)(char *, struct PyMethodDef *, char *, PyObject *, int);
static PyObject*(*dll_PyImport_AddModule)(char *);
static void(*dll_Py_SetPythonHome)(char *home);
static void(*dll_Py_Initialize)(void);
static void(*dll_Py_Finalize)(void);
static int(*dll_Py_IsInitialized)(void);
static PyObject*(*dll__PyObject_New)(PyTypeObject *, PyObject *);
static PyObject*(*dll__PyObject_Init)(PyObject *, PyTypeObject *);
static PyObject* dll__Py_NoneStruct;
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
static int (*dll_PyType_IsSubtype)(PyTypeObject *, PyTypeObject *);
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
static void* (*dll_PyObject_Malloc)(size_t);
static void (*dll_PyObject_Free)(void*);
# endif

static HINSTANCE hinstPython = 0; /* Instance of python.dll */

/* Imported exception objects */
static PyObject *imp_PyExc_AttributeError;
static PyObject *imp_PyExc_IndexError;
static PyObject *imp_PyExc_KeyboardInterrupt;
static PyObject *imp_PyExc_TypeError;
static PyObject *imp_PyExc_ValueError;

# define PyExc_AttributeError imp_PyExc_AttributeError
# define PyExc_IndexError imp_PyExc_IndexError
# define PyExc_KeyboardInterrupt imp_PyExc_KeyboardInterrupt
# define PyExc_TypeError imp_PyExc_TypeError
# define PyExc_ValueError imp_PyExc_ValueError

/*
 * Table of name to function pointer of python.
 */
# define PYTHON_PROC FARPROC
static struct
{
    char *name;
    PYTHON_PROC *ptr;
} python_funcname_table[] =
{
    {"PyArg_Parse", (PYTHON_PROC*)&dll_PyArg_Parse},
    {"PyArg_ParseTuple", (PYTHON_PROC*)&dll_PyArg_ParseTuple},
    {"PyMem_Free", (PYTHON_PROC*)&dll_PyMem_Free},
    {"PyDict_SetItemString", (PYTHON_PROC*)&dll_PyDict_SetItemString},
    {"PyErr_BadArgument", (PYTHON_PROC*)&dll_PyErr_BadArgument},
    {"PyErr_Clear", (PYTHON_PROC*)&dll_PyErr_Clear},
    {"PyErr_NoMemory", (PYTHON_PROC*)&dll_PyErr_NoMemory},
    {"PyErr_Occurred", (PYTHON_PROC*)&dll_PyErr_Occurred},
    {"PyErr_SetNone", (PYTHON_PROC*)&dll_PyErr_SetNone},
    {"PyErr_SetString", (PYTHON_PROC*)&dll_PyErr_SetString},
    {"PyEval_InitThreads", (PYTHON_PROC*)&dll_PyEval_InitThreads},
    {"PyEval_RestoreThread", (PYTHON_PROC*)&dll_PyEval_RestoreThread},
    {"PyEval_SaveThread", (PYTHON_PROC*)&dll_PyEval_SaveThread},
# ifdef PY_CAN_RECURSE
    {"PyGILState_Ensure", (PYTHON_PROC*)&dll_PyGILState_Ensure},
    {"PyGILState_Release", (PYTHON_PROC*)&dll_PyGILState_Release},
# endif
    {"PyInt_AsLong", (PYTHON_PROC*)&dll_PyInt_AsLong},
    {"PyInt_FromLong", (PYTHON_PROC*)&dll_PyInt_FromLong},
    {"PyInt_Type", (PYTHON_PROC*)&dll_PyInt_Type},
    {"PyList_GetItem", (PYTHON_PROC*)&dll_PyList_GetItem},
    {"PyList_Append", (PYTHON_PROC*)&dll_PyList_Append},
    {"PyList_New", (PYTHON_PROC*)&dll_PyList_New},
    {"PyList_SetItem", (PYTHON_PROC*)&dll_PyList_SetItem},
    {"PyList_Size", (PYTHON_PROC*)&dll_PyList_Size},
    {"PyList_Type", (PYTHON_PROC*)&dll_PyList_Type},
    {"PyTuple_GetItem", (PYTHON_PROC*)&dll_PyTuple_GetItem},
    {"PyTuple_Size", (PYTHON_PROC*)&dll_PyTuple_Size},
    {"PyTuple_Type", (PYTHON_PROC*)&dll_PyTuple_Type},
    {"PyImport_ImportModule", (PYTHON_PROC*)&dll_PyImport_ImportModule},
    {"PyDict_GetItemString", (PYTHON_PROC*)&dll_PyDict_GetItemString},
    {"PyDict_Items", (PYTHON_PROC*)&dll_PyDict_Items},
    {"PyDict_New", (PYTHON_PROC*)&dll_PyDict_New},
    {"PyModule_GetDict", (PYTHON_PROC*)&dll_PyModule_GetDict},
    {"PyRun_SimpleString", (PYTHON_PROC*)&dll_PyRun_SimpleString},
    {"PyRun_String", (PYTHON_PROC*)&dll_PyRun_String},
    {"PyString_AsString", (PYTHON_PROC*)&dll_PyString_AsString},
    {"PyString_FromString", (PYTHON_PROC*)&dll_PyString_FromString},
    {"PyString_FromStringAndSize", (PYTHON_PROC*)&dll_PyString_FromStringAndSize},
    {"PyString_Size", (PYTHON_PROC*)&dll_PyString_Size},
    {"PyString_Type", (PYTHON_PROC*)&dll_PyString_Type},
    {"PyFloat_Type", (PYTHON_PROC*)&dll_PyFloat_Type},
    {"PyFloat_AsDouble", (PYTHON_PROC*)&dll_PyFloat_AsDouble},
    {"PyFloat_FromDouble", (PYTHON_PROC*)&dll_PyFloat_FromDouble},
    {"PyImport_AddModule", (PYTHON_PROC*)&dll_PyImport_AddModule},
    {"PySys_SetObject", (PYTHON_PROC*)&dll_PySys_SetObject},
    {"PySys_SetArgv", (PYTHON_PROC*)&dll_PySys_SetArgv},
    {"PyType_Type", (PYTHON_PROC*)&dll_PyType_Type},
    {"PyType_Ready", (PYTHON_PROC*)&dll_PyType_Ready},
    {"Py_BuildValue", (PYTHON_PROC*)&dll_Py_BuildValue},
    {"Py_FindMethod", (PYTHON_PROC*)&dll_Py_FindMethod},
# if (PY_VERSION_HEX >= 0x02050000) && SIZEOF_SIZE_T != SIZEOF_INT
    {"Py_InitModule4_64", (PYTHON_PROC*)&dll_Py_InitModule4},
# else
    {"Py_InitModule4", (PYTHON_PROC*)&dll_Py_InitModule4},
# endif
    {"Py_SetPythonHome", (PYTHON_PROC*)&dll_Py_SetPythonHome},
    {"Py_Initialize", (PYTHON_PROC*)&dll_Py_Initialize},
    {"Py_Finalize", (PYTHON_PROC*)&dll_Py_Finalize},
    {"Py_IsInitialized", (PYTHON_PROC*)&dll_Py_IsInitialized},
    {"_PyObject_New", (PYTHON_PROC*)&dll__PyObject_New},
    {"PyObject_Init", (PYTHON_PROC*)&dll__PyObject_Init},
    {"_Py_NoneStruct", (PYTHON_PROC*)&dll__Py_NoneStruct},
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
    {"PyType_IsSubtype", (PYTHON_PROC*)&dll_PyType_IsSubtype},
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
    {"PyObject_Malloc", (PYTHON_PROC*)&dll_PyObject_Malloc},
    {"PyObject_Free", (PYTHON_PROC*)&dll_PyObject_Free},
# endif
    {"", NULL},
};

/*
 * Free python.dll
 */
    static void
end_dynamic_python(void)
{
    if (hinstPython)
    {
	close_dll(hinstPython);
	hinstPython = 0;
    }
}

/*
 * Load library and get all pointers.
 * Parameter 'libname' provides name of DLL.
 * Return OK or FAIL.
 */
    static int
python_runtime_link_init(char *libname, int verbose)
{
    int i;

#if !(defined(PY_NO_RTLD_GLOBAL) && defined(PY3_NO_RTLD_GLOBAL)) && defined(UNIX) && defined(FEAT_PYTHON3)
    /* Can't have Python and Python3 loaded at the same time.
     * It cause a crash, because RTLD_GLOBAL is needed for
     * standard C extension libraries of one or both python versions. */
    if (python3_loaded())
    {
	if (verbose)
	    EMSG(_("E836: This Vim cannot execute :python after using :py3"));
	return FAIL;
    }
#endif

    if (hinstPython)
	return OK;
    hinstPython = load_dll(libname);
    if (!hinstPython)
    {
	if (verbose)
	    EMSG2(_(e_loadlib), libname);
	return FAIL;
    }

    for (i = 0; python_funcname_table[i].ptr; ++i)
    {
	if ((*python_funcname_table[i].ptr = symbol_from_dll(hinstPython,
			python_funcname_table[i].name)) == NULL)
	{
	    close_dll(hinstPython);
	    hinstPython = 0;
	    if (verbose)
		EMSG2(_(e_loadfunc), python_funcname_table[i].name);
	    return FAIL;
	}
    }
    return OK;
}

/*
 * If python is enabled (there is installed python on Windows system) return
 * TRUE, else FALSE.
 */
    int
python_enabled(int verbose)
{
    return python_runtime_link_init(DYNAMIC_PYTHON_DLL, verbose) == OK;
}

/*
 * Load the standard Python exceptions - don't import the symbols from the
 * DLL, as this can cause errors (importing data symbols is not reliable).
 */
    static void
get_exceptions(void)
{
    PyObject *exmod = PyImport_ImportModule("exceptions");
    PyObject *exdict = PyModule_GetDict(exmod);
    imp_PyExc_AttributeError = PyDict_GetItemString(exdict, "AttributeError");
    imp_PyExc_IndexError = PyDict_GetItemString(exdict, "IndexError");
    imp_PyExc_KeyboardInterrupt = PyDict_GetItemString(exdict, "KeyboardInterrupt");
    imp_PyExc_TypeError = PyDict_GetItemString(exdict, "TypeError");
    imp_PyExc_ValueError = PyDict_GetItemString(exdict, "ValueError");
    Py_XINCREF(imp_PyExc_AttributeError);
    Py_XINCREF(imp_PyExc_IndexError);
    Py_XINCREF(imp_PyExc_KeyboardInterrupt);
    Py_XINCREF(imp_PyExc_TypeError);
    Py_XINCREF(imp_PyExc_ValueError);
    Py_XDECREF(exmod);
}
#endif /* DYNAMIC_PYTHON */

static PyObject *BufferNew (buf_T *);
static PyObject *WindowNew(win_T *);
static PyObject *DictionaryNew(dict_T *);
static PyObject *LineToString(const char *);

static PyTypeObject RangeType;

/*
 * Include the code shared with if_python3.c
 */
#include "if_py_both.h"


/******************************************************
 * Internal function prototypes.
 */

static PyInt RangeStart;
static PyInt RangeEnd;

static PyObject *globals;
static pyhashtab_T dictrefs;
static pyhashtab_T listrefs;

static void PythonIO_Flush(void);
static int PythonIO_Init(void);
static int PythonMod_Init(void);

/* Utility functions for the vim/python interface
 * ----------------------------------------------
 */

static int SetBufferLineList(buf_T *, PyInt, PyInt, PyObject *, PyInt *);


/******************************************************
 * 1. Python interpreter main program.
 */

static int initialised = 0;

#if PYTHON_API_VERSION < 1007 /* Python 1.4 */
typedef PyObject PyThreadState;
#endif

#ifdef PY_CAN_RECURSE
static PyGILState_STATE pygilstate = PyGILState_UNLOCKED;
#else
static PyThreadState *saved_python_thread = NULL;
#endif

/*
 * Suspend a thread of the Python interpreter, other threads are allowed to
 * run.
 */
    static void
Python_SaveThread(void)
{
#ifdef PY_CAN_RECURSE
    PyGILState_Release(pygilstate);
#else
    saved_python_thread = PyEval_SaveThread();
#endif
}

/*
 * Restore a thread of the Python interpreter, waits for other threads to
 * block.
 */
    static void
Python_RestoreThread(void)
{
#ifdef PY_CAN_RECURSE
    pygilstate = PyGILState_Ensure();
#else
    PyEval_RestoreThread(saved_python_thread);
    saved_python_thread = NULL;
#endif
}

    void
python_end()
{
    static int recurse = 0;

    /* If a crash occurs while doing this, don't try again. */
    if (recurse != 0)
	return;

    ++recurse;

#ifdef DYNAMIC_PYTHON
    if (hinstPython && Py_IsInitialized())
    {
	Python_RestoreThread();	    /* enter python */
	Py_Finalize();
    }
    end_dynamic_python();
#else
    if (Py_IsInitialized())
    {
	Python_RestoreThread();	    /* enter python */
	Py_Finalize();
    }
#endif

    --recurse;
}

#if (defined(DYNAMIC_PYTHON) && defined(FEAT_PYTHON3)) || defined(PROTO)
    int
python_loaded()
{
    return (hinstPython != 0);
}
#endif

    static int
Python_Init(void)
{
    if (!initialised)
    {
#ifdef DYNAMIC_PYTHON
	if (!python_enabled(TRUE))
	{
	    EMSG(_("E263: Sorry, this command is disabled, the Python library could not be loaded."));
	    goto fail;
	}
#endif

#ifdef PYTHON_HOME
	Py_SetPythonHome(PYTHON_HOME);
#endif

	init_structs();

#if !defined(MACOS) || defined(MACOS_X_UNIX)
	Py_Initialize();
#else
	PyMac_Initialize();
#endif
	/* initialise threads */
	PyEval_InitThreads();

#ifdef DYNAMIC_PYTHON
	get_exceptions();
#endif

	if (PythonIO_Init())
	    goto fail;

	if (PythonMod_Init())
	    goto fail;

	globals = PyModule_GetDict(PyImport_AddModule("__main__"));
	pyhash_init(&dictrefs);
	pyhash_init(&listrefs);

	/* Remove the element from sys.path that was added because of our
	 * argv[0] value in PythonMod_Init().  Previously we used an empty
	 * string, but dependinding on the OS we then get an empty entry or
	 * the current directory in sys.path. */
	PyRun_SimpleString("import sys; sys.path = filter(lambda x: x != '/must>not&exist', sys.path)");

	/* the first python thread is vim's, release the lock */
	Python_SaveThread();

	initialised = 1;
    }

    return 0;

fail:
    /* We call PythonIO_Flush() here to print any Python errors.
     * This is OK, as it is possible to call this function even
     * if PythonIO_Init() has not completed successfully (it will
     * not do anything in this case).
     */
    PythonIO_Flush();
    return -1;
}

/*
 * External interface
 */
    static void *
DoPythonCommand(exarg_T *eap, const char *cmd, int is_pyeval)
{
    void *r;
#ifndef PY_CAN_RECURSE
    static int		recursive = 0;
#endif
#if defined(MACOS) && !defined(MACOS_X_UNIX)
    GrafPtr		oldPort;
#endif
#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    char		*saved_locale;
#endif

#ifndef PY_CAN_RECURSE
    if (recursive)
    {
	EMSG(_("E659: Cannot invoke Python recursively"));
	return;
    }
    ++recursive;
#endif

#if defined(MACOS) && !defined(MACOS_X_UNIX)
    GetPort(&oldPort);
    /* Check if the Python library is available */
    if ((Ptr)PyMac_Initialize == (Ptr)kUnresolvedCFragSymbolAddress)
	goto theend;
#endif
    if (Python_Init())
	goto theend;

    if(is_pyeval)
    {
	RangeStart = (PyInt) curwin->w_cursor.lnum;
	RangeEnd = RangeStart;
    }
    else {
	RangeStart = eap->line1;
	RangeEnd = eap->line2;
    }
    Python_Release_Vim();	    /* leave vim */

#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    /* Python only works properly when the LC_NUMERIC locale is "C". */
    saved_locale = setlocale(LC_NUMERIC, NULL);
    if (saved_locale == NULL || STRCMP(saved_locale, "C") == 0)
	saved_locale = NULL;
    else
    {
	/* Need to make a copy, value may change when setting new locale. */
	saved_locale = (char *)vim_strsave((char_u *)saved_locale);
	(void)setlocale(LC_NUMERIC, "C");
    }
#endif

    Python_RestoreThread();	    /* enter python */

    if(is_pyeval)
    {
	r = (void *) PyRun_String((char *)cmd, Py_eval_input, globals, globals);
    }
    else{
	r = NULL;
	PyRun_SimpleString((char *)(cmd));
    }

    Python_SaveThread();	    /* leave python */

#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    if (saved_locale != NULL)
    {
	(void)setlocale(LC_NUMERIC, saved_locale);
	vim_free(saved_locale);
    }
#endif

    Python_Lock_Vim();		    /* enter vim */
    PythonIO_Flush();
#if defined(MACOS) && !defined(MACOS_X_UNIX)
    SetPort(oldPort);
#endif

theend:
#ifndef PY_CAN_RECURSE
    --recursive;
#endif
    return r;
}

/*
 * ":python"
 */
    void
ex_python(exarg_T *eap)
{
    char_u *script;

    script = script_get(eap, eap->arg);
    if (!eap->skip)
    {
	if (script == NULL)
	    DoPythonCommand(eap, (char *)eap->arg, 0);
	else
	    DoPythonCommand(eap, (char *)script, 0);
    }
    vim_free(script);
}

#define BUFFER_SIZE 1024

/*
 * ":pyfile"
 */
    void
ex_pyfile(exarg_T *eap)
{
    static char buffer[BUFFER_SIZE];
    const char *file = (char *)eap->arg;
    char *p;

    /* Have to do it like this. PyRun_SimpleFile requires you to pass a
     * stdio file pointer, but Vim and the Python DLL are compiled with
     * different options under Windows, meaning that stdio pointers aren't
     * compatible between the two. Yuk.
     *
     * Put the string "execfile('file')" into buffer. But, we need to
     * escape any backslashes or single quotes in the file name, so that
     * Python won't mangle the file name.
     */
    strcpy(buffer, "execfile('");
    p = buffer + 10; /* size of "execfile('" */

    while (*file && p < buffer + (BUFFER_SIZE - 3))
    {
	if (*file == '\\' || *file == '\'')
	    *p++ = '\\';
	*p++ = *file++;
    }

    /* If we didn't finish the file name, we hit a buffer overflow */
    if (*file != '\0')
	return;

    /* Put in the terminating "')" and a null */
    *p++ = '\'';
    *p++ = ')';
    *p++ = '\0';

    /* Execute the file */
    DoPythonCommand(eap, buffer, 0);
}

/******************************************************
 * 2. Python output stream: writes output via [e]msg().
 */

/* Implementation functions
 */

    static PyObject *
OutputGetattr(PyObject *self, char *name)
{
    if (strcmp(name, "softspace") == 0)
	return PyInt_FromLong(((OutputObject *)(self))->softspace);

    return Py_FindMethod(OutputMethods, self, name);
}

    static int
OutputSetattr(PyObject *self, char *name, PyObject *val)
{
    if (val == NULL)
    {
	PyErr_SetString(PyExc_AttributeError, _("can't delete OutputObject attributes"));
	return -1;
    }

    if (strcmp(name, "softspace") == 0)
    {
	if (!PyInt_Check(val))
	{
	    PyErr_SetString(PyExc_TypeError, _("softspace must be an integer"));
	    return -1;
	}

	((OutputObject *)(self))->softspace = PyInt_AsLong(val);
	return 0;
    }

    PyErr_SetString(PyExc_AttributeError, _("invalid attribute"));
    return -1;
}

/***************/

    static int
PythonIO_Init(void)
{
    /* Fixups... */
    PyType_Ready(&OutputType);

    return PythonIO_Init_io();
}

/******************************************************
 * 3. Implementation of the Vim module for Python
 */

static PyObject *ConvertToPyObject(typval_T *);
static int ConvertFromPyObject(PyObject *, typval_T *, int);

/* Window type - Implementation functions
 * --------------------------------------
 */

#define WindowType_Check(obj) ((obj)->ob_type == &WindowType)

static void WindowDestructor(PyObject *);
static PyObject *WindowGetattr(PyObject *, char *);

/* Buffer type - Implementation functions
 * --------------------------------------
 */

#define BufferType_Check(obj) ((obj)->ob_type == &BufferType)

static void BufferDestructor(PyObject *);
static PyObject *BufferGetattr(PyObject *, char *);
static PyObject *BufferRepr(PyObject *);

static PyInt BufferLength(PyObject *);
static PyObject *BufferItem(PyObject *, PyInt);
static PyObject *BufferSlice(PyObject *, PyInt, PyInt);
static PyInt BufferAssItem(PyObject *, PyInt, PyObject *);
static PyInt BufferAssSlice(PyObject *, PyInt, PyInt, PyObject *);

/* Line range type - Implementation functions
 * --------------------------------------
 */

#define RangeType_Check(obj) ((obj)->ob_type == &RangeType)

static PyInt RangeAssItem(PyObject *, PyInt, PyObject *);
static PyInt RangeAssSlice(PyObject *, PyInt, PyInt, PyObject *);

/* Current objects type - Implementation functions
 * -----------------------------------------------
 */

static PyObject *CurrentGetattr(PyObject *, char *);
static int CurrentSetattr(PyObject *, char *, PyObject *);

static PySequenceMethods BufferAsSeq = {
    (PyInquiry)		BufferLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0, /* BufferConcat, */	     /* sq_concat,    x+y      */
    (PyIntArgFunc)	0, /* BufferRepeat, */	     /* sq_repeat,    x*n      */
    (PyIntArgFunc)	BufferItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	BufferSlice,	    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	BufferAssItem,	    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc)	BufferAssSlice,     /* sq_ass_slice, x[i:j]=v */
};

static PyTypeObject BufferType = {
    PyObject_HEAD_INIT(0)
    0,
    "buffer",
    sizeof(BufferObject),
    0,

    (destructor)    BufferDestructor,	/* tp_dealloc,	refcount==0  */
    (printfunc)     0,			/* tp_print,	print x      */
    (getattrfunc)   BufferGetattr,	/* tp_getattr,	x.attr	     */
    (setattrfunc)   0,			/* tp_setattr,	x.attr=v     */
    (cmpfunc)	    0,			/* tp_compare,	x>y	     */
    (reprfunc)	    BufferRepr,		/* tp_repr,	`x`, print x */

    0,		    /* as number */
    &BufferAsSeq,   /* as sequence */
    0,		    /* as mapping */

    (hashfunc) 0,			/* tp_hash, dict(x) */
    (ternaryfunc) 0,			/* tp_call, x()     */
    (reprfunc) 0,			/* tp_str,  str(x)  */
};

/* Buffer object - Implementation
 */

    static PyObject *
BufferNew(buf_T *buf)
{
    /* We need to handle deletion of buffers underneath us.
     * If we add a "b_python_ref" field to the buf_T structure,
     * then we can get at it in buf_freeall() in vim. We then
     * need to create only ONE Python object per buffer - if
     * we try to create a second, just INCREF the existing one
     * and return it. The (single) Python object referring to
     * the buffer is stored in "b_python_ref".
     * Question: what to do on a buf_freeall(). We'll probably
     * have to either delete the Python object (DECREF it to
     * zero - a bad idea, as it leaves dangling refs!) or
     * set the buf_T * value to an invalid value (-1?), which
     * means we need checks in all access functions... Bah.
     */

    BufferObject *self;

    if (buf->b_python_ref != NULL)
    {
	self = buf->b_python_ref;
	Py_INCREF(self);
    }
    else
    {
	self = PyObject_NEW(BufferObject, &BufferType);
	if (self == NULL)
	    return NULL;
	self->buf = buf;
	buf->b_python_ref = self;
    }

    return (PyObject *)(self);
}

    static void
BufferDestructor(PyObject *self)
{
    BufferObject *this = (BufferObject *)(self);

    if (this->buf && this->buf != INVALID_BUFFER_VALUE)
	this->buf->b_python_ref = NULL;

    Py_DECREF(self);
}

    static PyObject *
BufferGetattr(PyObject *self, char *name)
{
    BufferObject *this = (BufferObject *)(self);

    if (CheckBuffer(this))
	return NULL;

    if (strcmp(name, "name") == 0)
	return Py_BuildValue("s", this->buf->b_ffname);
    else if (strcmp(name, "number") == 0)
	return Py_BuildValue(Py_ssize_t_fmt, this->buf->b_fnum);
    else if (strcmp(name,"__members__") == 0)
	return Py_BuildValue("[ss]", "name", "number");
    else
	return Py_FindMethod(BufferMethods, self, name);
}

    static PyObject *
BufferRepr(PyObject *self)
{
    static char repr[100];
    BufferObject *this = (BufferObject *)(self);

    if (this->buf == INVALID_BUFFER_VALUE)
    {
	vim_snprintf(repr, 100, _("<buffer object (deleted) at %p>"), (self));
	return PyString_FromString(repr);
    }
    else
    {
	char *name = (char *)this->buf->b_fname;
	PyInt len;

	if (name == NULL)
	    name = "";
	len = strlen(name);

	if (len > 35)
	    name = name + (35 - len);

	vim_snprintf(repr, 100, "<buffer %s%s>", len > 35 ? "..." : "", name);

	return PyString_FromString(repr);
    }
}

/******************/

    static PyInt
BufferLength(PyObject *self)
{
    /* HOW DO WE SIGNAL AN ERROR FROM THIS FUNCTION? */
    if (CheckBuffer((BufferObject *)(self)))
	return -1; /* ??? */

    return (((BufferObject *)(self))->buf->b_ml.ml_line_count);
}

    static PyObject *
BufferItem(PyObject *self, PyInt n)
{
    return RBItem((BufferObject *)(self), n, 1,
		  (int)((BufferObject *)(self))->buf->b_ml.ml_line_count);
}

    static PyObject *
BufferSlice(PyObject *self, PyInt lo, PyInt hi)
{
    return RBSlice((BufferObject *)(self), lo, hi, 1,
		   (int)((BufferObject *)(self))->buf->b_ml.ml_line_count);
}

    static PyInt
BufferAssItem(PyObject *self, PyInt n, PyObject *val)
{
    return RBAsItem((BufferObject *)(self), n, val, 1,
		     (PyInt)((BufferObject *)(self))->buf->b_ml.ml_line_count,
		     NULL);
}

    static PyInt
BufferAssSlice(PyObject *self, PyInt lo, PyInt hi, PyObject *val)
{
    return RBAsSlice((BufferObject *)(self), lo, hi, val, 1,
		      (PyInt)((BufferObject *)(self))->buf->b_ml.ml_line_count,
		      NULL);
}

static PySequenceMethods RangeAsSeq = {
    (PyInquiry)		RangeLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0, /* RangeConcat, */	     /* sq_concat,    x+y      */
    (PyIntArgFunc)	0, /* RangeRepeat, */	     /* sq_repeat,    x*n      */
    (PyIntArgFunc)	RangeItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	RangeSlice,	    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	RangeAssItem,	    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc)	RangeAssSlice,	    /* sq_ass_slice, x[i:j]=v */
};

/* Line range object - Implementation
 */

    static void
RangeDestructor(PyObject *self)
{
    Py_DECREF(((RangeObject *)(self))->buf);
    Py_DECREF(self);
}

    static PyObject *
RangeGetattr(PyObject *self, char *name)
{
    if (strcmp(name, "start") == 0)
	return Py_BuildValue(Py_ssize_t_fmt, ((RangeObject *)(self))->start - 1);
    else if (strcmp(name, "end") == 0)
	return Py_BuildValue(Py_ssize_t_fmt, ((RangeObject *)(self))->end - 1);
    else
	return Py_FindMethod(RangeMethods, self, name);
}

/****************/

    static PyInt
RangeAssItem(PyObject *self, PyInt n, PyObject *val)
{
    return RBAsItem(((RangeObject *)(self))->buf, n, val,
		     ((RangeObject *)(self))->start,
		     ((RangeObject *)(self))->end,
		     &((RangeObject *)(self))->end);
}

    static PyInt
RangeAssSlice(PyObject *self, PyInt lo, PyInt hi, PyObject *val)
{
    return RBAsSlice(((RangeObject *)(self))->buf, lo, hi, val,
		      ((RangeObject *)(self))->start,
		      ((RangeObject *)(self))->end,
		      &((RangeObject *)(self))->end);
}

/* Buffer list object - Definitions
 */

typedef struct
{
    PyObject_HEAD
} BufListObject;

static PySequenceMethods BufListAsSeq = {
    (PyInquiry)		BufListLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0,		    /* sq_concat,    x+y      */
    (PyIntArgFunc)	0,		    /* sq_repeat,    x*n      */
    (PyIntArgFunc)	BufListItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	0,		    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	0,		    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc)	0,		    /* sq_ass_slice, x[i:j]=v */
};

static PyTypeObject BufListType = {
    PyObject_HEAD_INIT(0)
    0,
    "buffer list",
    sizeof(BufListObject),
    0,

    (destructor)    0,			/* tp_dealloc,	refcount==0  */
    (printfunc)     0,			/* tp_print,	print x      */
    (getattrfunc)   0,			/* tp_getattr,	x.attr	     */
    (setattrfunc)   0,			/* tp_setattr,	x.attr=v     */
    (cmpfunc)	    0,			/* tp_compare,	x>y	     */
    (reprfunc)	    0,			/* tp_repr,	`x`, print x */

    0,		    /* as number */
    &BufListAsSeq,  /* as sequence */
    0,		    /* as mapping */

    (hashfunc) 0,			/* tp_hash, dict(x) */
    (ternaryfunc) 0,			/* tp_call, x()     */
    (reprfunc) 0,			/* tp_str,  str(x)  */
};

/* Window object - Definitions
 */

static struct PyMethodDef WindowMethods[] = {
    /* name,	    function,		calling,    documentation */
    { NULL,	    NULL,		0,	    NULL }
};

static PyTypeObject WindowType = {
    PyObject_HEAD_INIT(0)
    0,
    "window",
    sizeof(WindowObject),
    0,

    (destructor)    WindowDestructor,	/* tp_dealloc,	refcount==0  */
    (printfunc)     0,			/* tp_print,	print x      */
    (getattrfunc)   WindowGetattr,	/* tp_getattr,	x.attr	     */
    (setattrfunc)   WindowSetattr,	/* tp_setattr,	x.attr=v     */
    (cmpfunc)	    0,			/* tp_compare,	x>y	     */
    (reprfunc)	    WindowRepr,		/* tp_repr,	`x`, print x */

    0,		    /* as number */
    0,		    /* as sequence */
    0,		    /* as mapping */

    (hashfunc) 0,			/* tp_hash, dict(x) */
    (ternaryfunc) 0,			/* tp_call, x()     */
    (reprfunc) 0,			/* tp_str,  str(x)  */
};

/* Window object - Implementation
 */

    static PyObject *
WindowNew(win_T *win)
{
    /* We need to handle deletion of windows underneath us.
     * If we add a "w_python_ref" field to the win_T structure,
     * then we can get at it in win_free() in vim. We then
     * need to create only ONE Python object per window - if
     * we try to create a second, just INCREF the existing one
     * and return it. The (single) Python object referring to
     * the window is stored in "w_python_ref".
     * On a win_free() we set the Python object's win_T* field
     * to an invalid value. We trap all uses of a window
     * object, and reject them if the win_T* field is invalid.
     */

    WindowObject *self;

    if (win->w_python_ref)
    {
	self = win->w_python_ref;
	Py_INCREF(self);
    }
    else
    {
	self = PyObject_NEW(WindowObject, &WindowType);
	if (self == NULL)
	    return NULL;
	self->win = win;
	win->w_python_ref = self;
    }

    return (PyObject *)(self);
}

    static void
WindowDestructor(PyObject *self)
{
    WindowObject *this = (WindowObject *)(self);

    if (this->win && this->win != INVALID_WINDOW_VALUE)
	this->win->w_python_ref = NULL;

    Py_DECREF(self);
}

    static PyObject *
WindowGetattr(PyObject *self, char *name)
{
    WindowObject *this = (WindowObject *)(self);

    if (CheckWindow(this))
	return NULL;

    if (strcmp(name, "buffer") == 0)
	return (PyObject *)BufferNew(this->win->w_buffer);
    else if (strcmp(name, "cursor") == 0)
    {
	pos_T *pos = &this->win->w_cursor;

	return Py_BuildValue("(ll)", (long)(pos->lnum), (long)(pos->col));
    }
    else if (strcmp(name, "height") == 0)
	return Py_BuildValue("l", (long)(this->win->w_height));
#ifdef FEAT_VERTSPLIT
    else if (strcmp(name, "width") == 0)
	return Py_BuildValue("l", (long)(W_WIDTH(this->win)));
#endif
    else if (strcmp(name,"__members__") == 0)
	return Py_BuildValue("[sss]", "buffer", "cursor", "height");
    else
	return Py_FindMethod(WindowMethods, self, name);
}

/* Window list object - Definitions
 */

typedef struct
{
    PyObject_HEAD
}
WinListObject;

static PySequenceMethods WinListAsSeq = {
    (PyInquiry)		WinListLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0,		    /* sq_concat,    x+y      */
    (PyIntArgFunc)	0,		    /* sq_repeat,    x*n      */
    (PyIntArgFunc)	WinListItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	0,		    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	0,		    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc)	0,		    /* sq_ass_slice, x[i:j]=v */
};

static PyTypeObject WinListType = {
    PyObject_HEAD_INIT(0)
    0,
    "window list",
    sizeof(WinListObject),
    0,

    (destructor)    0,			/* tp_dealloc,	refcount==0  */
    (printfunc)     0,			/* tp_print,	print x      */
    (getattrfunc)   0,			/* tp_getattr,	x.attr	     */
    (setattrfunc)   0,			/* tp_setattr,	x.attr=v     */
    (cmpfunc)	    0,			/* tp_compare,	x>y	     */
    (reprfunc)	    0,			/* tp_repr,	`x`, print x */

    0,		    /* as number */
    &WinListAsSeq,  /* as sequence */
    0,		    /* as mapping */

    (hashfunc) 0,			/* tp_hash, dict(x) */
    (ternaryfunc) 0,			/* tp_call, x()     */
    (reprfunc) 0,			/* tp_str,  str(x)  */
};

/* Current items object - Definitions
 */

typedef struct
{
    PyObject_HEAD
} CurrentObject;

static PyTypeObject CurrentType = {
    PyObject_HEAD_INIT(0)
    0,
    "current data",
    sizeof(CurrentObject),
    0,

    (destructor)    0,			/* tp_dealloc,	refcount==0  */
    (printfunc)     0,			/* tp_print,	print x      */
    (getattrfunc)   CurrentGetattr,	/* tp_getattr,	x.attr	     */
    (setattrfunc)   CurrentSetattr,	/* tp_setattr,	x.attr=v     */
    (cmpfunc)	    0,			/* tp_compare,	x>y	     */
    (reprfunc)	    0,			/* tp_repr,	`x`, print x */

    0,		    /* as number */
    0,		    /* as sequence */
    0,		    /* as mapping */

    (hashfunc) 0,			/* tp_hash, dict(x) */
    (ternaryfunc) 0,			/* tp_call, x()     */
    (reprfunc) 0,			/* tp_str,  str(x)  */
};

/* Current items object - Implementation
 */
    static PyObject *
CurrentGetattr(PyObject *self UNUSED, char *name)
{
    if (strcmp(name, "buffer") == 0)
	return (PyObject *)BufferNew(curbuf);
    else if (strcmp(name, "window") == 0)
	return (PyObject *)WindowNew(curwin);
    else if (strcmp(name, "line") == 0)
	return GetBufferLine(curbuf, (PyInt)curwin->w_cursor.lnum);
    else if (strcmp(name, "range") == 0)
	return RangeNew(curbuf, RangeStart, RangeEnd);
    else if (strcmp(name,"__members__") == 0)
	return Py_BuildValue("[ssss]", "buffer", "window", "line", "range");
    else
    {
	PyErr_SetString(PyExc_AttributeError, name);
	return NULL;
    }
}

    static int
CurrentSetattr(PyObject *self UNUSED, char *name, PyObject *value)
{
    if (strcmp(name, "line") == 0)
    {
	if (SetBufferLine(curbuf, (PyInt)curwin->w_cursor.lnum, value, NULL) == FAIL)
	    return -1;

	return 0;
    }
    else
    {
	PyErr_SetString(PyExc_AttributeError, name);
	return -1;
    }
}

/* External interface
 */

    void
python_buffer_free(buf_T *buf)
{
    if (buf->b_python_ref != NULL)
    {
	BufferObject *bp = buf->b_python_ref;
	bp->buf = INVALID_BUFFER_VALUE;
	buf->b_python_ref = NULL;
    }
}

#if defined(FEAT_WINDOWS) || defined(PROTO)
    void
python_window_free(win_T *win)
{
    if (win->w_python_ref != NULL)
    {
	WindowObject *wp = win->w_python_ref;
	wp->win = INVALID_WINDOW_VALUE;
	win->w_python_ref = NULL;
    }
}
#endif

static BufListObject TheBufferList =
{
    PyObject_HEAD_INIT(&BufListType)
};

static WinListObject TheWindowList =
{
    PyObject_HEAD_INIT(&WinListType)
};

static CurrentObject TheCurrent =
{
    PyObject_HEAD_INIT(&CurrentType)
};

    static int
PythonMod_Init(void)
{
    PyObject *mod;
    PyObject *dict;
    /* The special value is removed from sys.path in Python_Init(). */
    static char *(argv[2]) = {"/must>not&exist/foo", NULL};

    /* Fixups... */
    PyType_Ready(&BufferType);
    PyType_Ready(&RangeType);
    PyType_Ready(&WindowType);
    PyType_Ready(&BufListType);
    PyType_Ready(&WinListType);
    PyType_Ready(&CurrentType);

    /* Set sys.argv[] to avoid a crash in warn(). */
    PySys_SetArgv(1, argv);

    mod = Py_InitModule4("vim", VimMethods, (char *)NULL, (PyObject *)NULL, PYTHON_API_VERSION);
    dict = PyModule_GetDict(mod);

    VimError = Py_BuildValue("s", "vim.error");

    PyDict_SetItemString(dict, "error", VimError);
    PyDict_SetItemString(dict, "buffers", (PyObject *)(void *)&TheBufferList);
    PyDict_SetItemString(dict, "current", (PyObject *)(void *)&TheCurrent);
    PyDict_SetItemString(dict, "windows", (PyObject *)(void *)&TheWindowList);

    if (PyErr_Occurred())
	return -1;

    return 0;
}

/*************************************************************************
 * 4. Utility functions for handling the interface between Vim and Python.
 */

/* Convert a Vim line into a Python string.
 * All internal newlines are replaced by null characters.
 *
 * On errors, the Python exception data is set, and NULL is returned.
 */
    static PyObject *
LineToString(const char *str)
{
    PyObject *result;
    PyInt len = strlen(str);
    char *p;

    /* Allocate an Python string object, with uninitialised contents. We
     * must do it this way, so that we can modify the string in place
     * later. See the Python source, Objects/stringobject.c for details.
     */
    result = PyString_FromStringAndSize(NULL, len);
    if (result == NULL)
	return NULL;

    p = PyString_AsString(result);

    while (*str)
    {
	if (*str == '\n')
	    *p = '\0';
	else
	    *p = *str;

	++p;
	++str;
    }

    return result;
}

static void DictionaryDestructor(PyObject *);
static PyInt DictionaryLength(PyObject *);
static PyObject *DictionaryItem(PyObject *, PyObject *);
static PyInt DictionaryAssItem(PyObject *, PyObject *, PyObject *);

static PyMappingMethods DictionaryAsMapping = {
    (PyInquiry)		DictionaryLength,
    (binaryfunc)	DictionaryItem,
    (objobjargproc)	DictionaryAssItem,
};

static PyTypeObject DictionaryType = {
    PyObject_HEAD_INIT(0)
    0,
    "vimdictionary",
    sizeof(DictionaryObject),
    0,

    (destructor)  DictionaryDestructor,
    (printfunc)   0,
    (getattrfunc) 0,
    (setattrfunc) 0,
    (cmpfunc)     0,
    (reprfunc)    0,

    0,                      /* as number */
    0,                      /* as sequence */
    &DictionaryAsMapping,   /* as mapping */

    (hashfunc)    0,
    (ternaryfunc) 0,
    (reprfunc)    0,
};

    static PyObject *
DictionaryNew(dict_T *dict)
{
    DictionaryObject	*self;
    void	**hi = NULL;

    hi = pyhash_lookup(&dictrefs, (void *) dict);
    if(hi == NULL)
    {
	PyErr_SetVim(_("internal error: failed to find a place for dictionary"));
	return NULL;
    }
    if(*hi != NULL)
	self = (DictionaryObject *) PHVAL(dictrefs, hi);
    if(*hi == NULL || self->ob_refcnt<=0)
    {
	self = PyObject_NEW(DictionaryObject, &DictionaryType);
	if (self == NULL)
	    return NULL;
	self->dict = dict;
	++dict->dv_refcount;
	pyhash_add_item(&dictrefs, hi, (void *) dict, (PyObject *) self);
    }
    else
	Py_INCREF(self);
    return (PyObject *)(self);
}

    static void
DictionaryDestructor(PyObject *self)
{
    void	**hi = NULL;
    DictionaryObject	*this = ((DictionaryObject *) (self));

    hi = pyhash_lookup(&dictrefs, (void *) this->dict);
    if(hi != NULL && *hi == this->dict)
	pyhash_remove(&dictrefs, hi);
    dict_unref(this->dict);

    Py_DECREF(self);
}

    static PyInt
DictionaryLength(PyObject *self)
{
    return ((PyInt) ((((DictionaryObject *)(self))->dict->dv_hashtab.ht_used)));
}

    static PyObject *
DictionaryItem(PyObject *self, PyObject *keyObject)
{
    char_u	*key;
    dictitem_T	*val;

    key = (char_u *) PyString_AsString(keyObject);
    val = dict_find(((DictionaryObject *) (self))->dict, key, -1);

    return ConvertToPyObject(&val->di_tv);
}

    static PyInt
DictionaryAssItem(PyObject *self, PyObject *keyObject, PyObject *valObject)
{
    char_u	*key;
    typval_T	tv;
    dict_T	*d = ((DictionaryObject *)(self))->dict;
    dictitem_T	*di;

    if(d->dv_lock)
    {
	PyErr_SetVim(_("dict is locked"));
	return -1;
    }

    /* Add conversion from PyInt? */
    if(!PyString_Check(keyObject))
    {
	PyErr_SetString(PyExc_TypeError, _("only string keys are allowed"));
	return -1;
    }
    key = (char_u *) PyString_AsString(keyObject);

    di = dict_find(d, key, -1);

    if(valObject == NULL)
    {
	if(di == NULL)
	{
	    PyErr_SetString(PyExc_IndexError, _("no such key in dictionary"));
	    return -1;
	}
	hashitem_T	*hi = hash_find(&d->dv_hashtab, di->di_key);
	hash_remove(&d->dv_hashtab, hi);
	dictitem_free(di);
	return 0;
    }

    if(ConvertFromPyObject(valObject, &tv, 1) == -1)
    {
	return -1;
    }

    if(di == NULL)
    {
	di = dictitem_alloc(key);
	if(di == NULL)
	{
	    PyErr_NoMemory();
	    return -1;
	}
	if(dict_add(d, di) == FAIL)
	{
	    vim_free(di);
	    PyErr_SetVim(_("failed to add key to dictionary"));
	    return -1;
	}
    }
    else
	clear_tv(&di->di_tv);

    copy_tv(&tv, &di->di_tv);
    return 0;
}

#define OBJ_NULL_ERR(obj, str) if(obj==NULL) {if(raise) PyErr_SetVim(_(str)); return -1;}

    static int
list_py_concat(list_T *l, PyObject *obj, PyInquiry Size, PyIntArgFunc Item, int raise)
{
    Py_ssize_t	i;
    Py_ssize_t	lsize = Size(obj);
    PyObject	*litem;
    typval_T	v;

    for(i=0; i<lsize; i++)
    {
	litem = Item(obj, i);
	OBJ_NULL_ERR(litem, "internal error: no list item")
	if(ConvertFromPyObject(litem, &v, 1) == -1)
	{
	    return -1;
	}
	if(list_append_tv(l, &v) == FAIL)
	{
	    PyErr_SetVim(_("failed to add item to list"));
	    return -1;
	}
    }
    return 0;
}

/* FIXME Copy-paste from if_lua.c */
    static listitem_T *
list_find (list_T *l, long n)
{
    listitem_T *li;
    if (l == NULL || n < -l->lv_len || n >= l->lv_len)
	return NULL;
    if (n < 0) /* search backward? */
	for (li = l->lv_last; n < -1; li = li->li_prev)
	    n++;
    else /* search forward */
	for (li = l->lv_first; n > 0; li = li->li_next)
	    n--;
    return li;
}

/* FIXME Copy-paste from if_lua.c */
    static void
list_remove (list_T *l, listitem_T *li)
{
    listwatch_T *lw;
    --l->lv_len;
    /* fix watchers */
    for (lw = l->lv_watch; lw != NULL; lw = lw->lw_next)
	if (lw->lw_item == li)
	    lw->lw_item = li->li_next;
    /* fix list pointers */
    if (li->li_next == NULL) /* last? */
	l->lv_last = li->li_prev;
    else
	li->li_next->li_prev = li->li_prev;
    if (li->li_prev == NULL) /* first? */
	l->lv_first = li->li_next;
    else
	li->li_prev->li_next = li->li_next;
    l->lv_idx_item = NULL;
}

static void ListDestructor(PyObject *);
static PyInt ListLength(PyObject *);
static PyObject *ListItem(PyObject *, Py_ssize_t);
static PyObject *ListSlice(PyObject *, Py_ssize_t, Py_ssize_t);
static int ListAssItem(PyObject *, Py_ssize_t, PyObject *);
static int ListAssSlice(PyObject *, Py_ssize_t, Py_ssize_t, PyObject *);
static PyObject *ListConcatInPlace(PyObject *, PyObject *);
static PyObject *ListGetattr(PyObject *, char *);

static PySequenceMethods ListAsSeq = {
    (PyInquiry)			ListLength,
    (binaryfunc)		0,
    (PyIntArgFunc)		0,
    (PyIntArgFunc)		ListItem,
    (PyIntIntArgFunc)		ListSlice,
    (PyIntObjArgProc)		ListAssItem,
    (PyIntIntObjArgProc)	ListAssSlice,
    (objobjproc)		0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)		ListConcatInPlace,
#endif
};

static PyTypeObject ListType = {
    PyObject_HEAD_INIT(0)
    0,
    "vimlist",
    sizeof(ListObject),
    0,

    (destructor)  ListDestructor,
    (printfunc)   0,
    (getattrfunc) ListGetattr,
    (setattrfunc) 0,
    (cmpfunc)     0,
    (reprfunc)    0,

    0,                      /* as number */
    &ListAsSeq,             /* as sequence */
    0,                      /* as mapping */

    (hashfunc)    0,
    (ternaryfunc) 0,
    (reprfunc)    0,
};

    static PyObject *
ListNew(list_T *list)
{
    ListObject	*self;
    void	**hi = NULL;

    hi = pyhash_lookup(&listrefs, (void *) list);
    if(hi == NULL)
    {
	PyErr_SetVim(_("internal error: failed to find a place for list"));
	return NULL;
    }
    if(*hi != NULL)
	self = (ListObject *) PHVAL(listrefs, hi);
    if(*hi == NULL || self->ob_refcnt<=0)
    {
	self = PyObject_NEW(ListObject, &ListType);
	if (self == NULL)
	    return NULL;
	self->list = list;
	++list->lv_refcount;
	pyhash_add_item(&listrefs, hi, (void *) list, (PyObject *) self);
    }
    else
	Py_INCREF(self);
    return (PyObject *)(self);
}

    static void
ListDestructor(PyObject *self)
{
    void	**hi = NULL;
    ListObject	*this = ((ListObject *) (self));

    hi = pyhash_lookup(&listrefs, (void *) this->list);
    if(hi != NULL && *hi == this->list)
	pyhash_remove(&listrefs, hi);
    list_unref(this->list);

    Py_DECREF(self);
}

    static PyObject *
ListGetattr(PyObject *self, char *name)
{
    return Py_FindMethod(ListMethods, self, name);
}

    static PyInt
ListLength(PyObject *self)
{
    return ((PyInt) (((ListObject *) (self))->list->lv_len));
}

    static PyObject *
ListItem(PyObject *self, Py_ssize_t index)
{
    listitem_T	*li;

    if(index>=ListLength(self))
    {
	PyErr_SetString(PyExc_IndexError, "list index out of range");
	return NULL;
    }
    li = list_find(((ListObject *) (self))->list, (long) index);
    if(li == NULL)
    {
	PyErr_SetVim(_("internal error: failed to get vim list item"));
	return NULL;
    }
    return ConvertToPyObject(&li->li_tv);
}

#define PROC_RANGE \
    if(last < 0) {\
	if(last < -size) \
	    last = 0; \
	else \
	    last += size; \
    } \
    if(first < 0) \
	first = 0; \
    if(first > size) \
	first = size; \
    if(last > size) \
	last = size;

    static PyObject *
ListSlice(PyObject *self, Py_ssize_t first, Py_ssize_t last)
{
    PyInt	i;
    PyInt	size = ListLength(self);
    PyInt	n;
    PyObject	*list;
    int		reversed = 0;

    PROC_RANGE
    if(first >= last)
	first = last;

    n = last-first;
    list = PyList_New(n);
    if(list == NULL)
	return NULL;

    for (i = 0; i < n; ++i)
    {
	PyObject	*item = ListItem(self, i);
	if(item == NULL)
	{
	    Py_DECREF(list);
	    return NULL;
	}

	if((PyList_SetItem(list, ((reversed)?(n-i-1):(i)), item)))
	{
	    Py_DECREF(item);
	    Py_DECREF(list);
	    return NULL;
	}
    }

    return list;
}

    static int
ListAssItem(PyObject *self, Py_ssize_t index, PyObject *obj)
{
    typval_T	tv;
    list_T	*l = ((ListObject *) (self))->list;
    listitem_T	*li;
    Py_ssize_t	length = ListLength(self);

    if(l->lv_lock)
    {
	PyErr_SetVim(_("list is locked"));
	return -1;
    }
    if(index>length || (index==length && obj==NULL))
    {
	PyErr_SetString(PyExc_IndexError, "list index out of range");
	return -1;
    }

    if(obj == NULL)
    {
	li = list_find(l, (long) index);
	list_remove(l, li);
	clear_tv(&li->li_tv);
	vim_free(li);
	return 0;
    }

    if(ConvertFromPyObject(obj, &tv, 1) == -1)
	return -1;

    if(index == length)
    {
	if(list_append_tv(l, &tv) == FAIL)
	{
	    PyErr_SetVim(_("internal error: failed to add item to list"));
	    return -1;
	}
    }
    else {
	li = list_find(l, (long) index);
	clear_tv(&li->li_tv);
	copy_tv(&tv, &li->li_tv);
    }
    return 0;
}

    static int
ListAssSlice(PyObject *self, Py_ssize_t first, Py_ssize_t last, PyObject *obj)
{
    PyInt	size = ListLength(self);
    Py_ssize_t	i;
    Py_ssize_t	lsize;
    PyObject	*litem;
    listitem_T	*li;
    listitem_T	*next;
    typval_T	v;
    list_T	*l = ((ListObject *) (self))->list;

    if(l->lv_lock)
    {
	PyErr_SetVim(_("list is locked"));
	return -1;
    }

    PROC_RANGE

    if(first == size)
	li = NULL;
    else {
	li = list_find(l, (long) first);
	if(li == NULL)
	{
	    PyErr_SetVim(_("internal error: no vim list item"));
	    return -1;
	}
	if(last > first)
	{
	    i = last - first;
	    while(i-- && li != NULL)
	    {
		next = li->li_next;
		listitem_remove(l, li);
		li = next;
	    }
	}
    }

    if(obj == NULL)
	return 0;

    if(!PyList_Check(obj))
    {
	PyErr_SetString(PyExc_TypeError, _("can only assign lists to slice"));
	return -1;
    }

    lsize = PyList_Size(obj);

    for(i=0; i<lsize; i++)
    {
	litem = PyList_GetItem(obj, i);
	if(litem == NULL)
	{
	    PyErr_SetVim(_("internal error: no list item"));
	    return -1;
	}
	if(ConvertFromPyObject(litem, &v, 1) == -1)
	    return -1;
	if(list_insert_tv(l, &v, li) == FAIL)
	{
	    PyErr_SetVim(_("failed to add item to list"));
	    return -1;
	}
    }
    return 0;
}

    static PyObject *
ListConcatInPlace(PyObject *self, PyObject *obj)
{
    list_T	*l = ((ListObject *) (self))->list;

    if(l->lv_lock)
    {
	PyErr_SetVim(_("list is locked"));
	return NULL;
    }

    if(!PyList_Check(obj))
    {
	PyErr_SetString(PyExc_TypeError, _("can only concatenate with lists"));
	return NULL;
    }

    if(list_py_concat(l, obj, PyList_Size, PyList_GetItem, 1)==-1)
	return NULL;

    Py_INCREF(self);

    return self;
}

static void FunctionDestructor(PyObject *);
static PyObject *FunctionGetattr(PyObject *, char *);

static PyTypeObject FunctionType = {
    PyObject_HEAD_INIT(0)
    0,
    "vimfunction",
    sizeof(FunctionObject),
    0,

    (destructor)  FunctionDestructor,
    (printfunc)   0,
    (getattrfunc) FunctionGetattr,
    (setattrfunc) 0,
    (cmpfunc)     0,
    (reprfunc)    0,

    0,                      /* as number */
    0,                      /* as sequence */
    0,                      /* as mapping */

    (hashfunc)    0,
    (ternaryfunc) FunctionCall,
    (reprfunc)    0,
};

    static PyObject *
FunctionNew(char_u *name)
{
    FunctionObject	*self;

    self = PyObject_NEW(FunctionObject, &FunctionType);
    if(self == NULL)
	return NULL;
    self->name = (char_u *) alloc((char_u) sizeof(char_u)*STRLEN(name));
    if(self->name == NULL)
    {
	PyErr_NoMemory();
	return NULL;
    }
    STRCPY(self->name, name);
    func_ref(name);
    return (PyObject *)(self);
}

    static void
FunctionDestructor(PyObject *self)
{
    FunctionObject	*this = (FunctionObject *) (self);

    func_unref(this->name);
    vim_free(this->name);

    Py_DECREF(self);
}

    static PyObject *
FunctionGetattr(PyObject *self, char *name)
{
    FunctionObject	*this = (FunctionObject *)(self);

    if(strcmp(name, "name") == 0)
	return PyString_FromString((char *)(this->name));
    else
	return Py_FindMethod(FunctionMethods, self, name);
}

    static PyObject *
FunctionCall(PyObject *self, PyObject *argsObject, PyObject *kwargs)
{
    FunctionObject	*this = (FunctionObject *)(self);
    char_u	*name = this->name;
    typval_T	args;
    typval_T	selfdicttv;
    typval_T	rettv;
    dict_T	*selfdict = NULL;
    PyObject	*selfdictObject;
    PyObject	*result;

    if(ConvertFromPyObject(argsObject, &args, 1) == -1)
	return NULL;

    if(kwargs != NULL)
    {
	selfdictObject = PyDict_GetItemString(kwargs, "self");
	if(selfdictObject != NULL)
	{
	    if(ConvertFromPyObject(selfdictObject, &selfdicttv, 1) == -1)
		return NULL;
	    if(selfdicttv.v_type != VAR_DICT)
	    {
		PyErr_SetString(PyExc_TypeError, _("'self' argument must be a dictionary"));
		clear_tv(&args);
		clear_tv(&selfdicttv);
		return NULL;
	    }
	    selfdict = selfdicttv.vval.v_dict;
	}
    }

    func_call(name, &args, selfdict, &rettv);
    result = ConvertToPyObject(&rettv);

    /* FIXME Check what should really be cleared. */
    clear_tv(&args);
    clear_tv(&rettv);
    /*
     * if(selfdict!=NULL)
     *     clear_tv(selfdicttv);
     */

    return result;
}

    static PyObject *
ConvertToPyObject(typval_T *tv)
{
    if(tv == NULL)
    {
	PyErr_SetVim(_("NULL reference passed"));
	return NULL;
    }
    switch (tv->v_type)
    {
	case VAR_STRING:
	    return PyString_FromString((char *) tv->vval.v_string);
	case VAR_NUMBER:
	    return PyInt_FromLong((long) tv->vval.v_number);
#ifdef FEAT_FLOAT
	case VAR_FLOAT:
	    return PyFloat_FromDouble((double) tv->vval.v_float);
#endif
	case VAR_LIST:
	    return ListNew(tv->vval.v_list);
	case VAR_DICT:
	    return DictionaryNew(tv->vval.v_dict);
	case VAR_FUNC:
	    return FunctionNew(tv->vval.v_string);
	default:
	    PyErr_SetVim(_("internal error: invalid value type"));
	    return NULL;
    }
}

    static int
set_string_copy(char_u *str, typval_T *tv, int raise)
{
    char_u	*copy;

    copy = (char_u *) alloc(STRLEN(str)+1);
    if(copy == NULL)
    {
	if(raise)
	    PyErr_NoMemory();
	return -1;
    }

    STRCPY(copy, str);
    tv->vval.v_string = copy;
    return 0;
}

    static int
ConvertFromPyObject(PyObject *obj, typval_T *tv, int raise)
{
    if(obj->ob_type == &DictionaryType)
    {
	tv->v_type = VAR_DICT;
	tv->vval.v_dict = (((DictionaryObject *)(obj))->dict);
    }
    else if(obj->ob_type == &ListType)
    {
	tv->v_type = VAR_LIST;
	tv->vval.v_list = (((ListObject *)(obj))->list);
    }
    else if(obj->ob_type == &FunctionType)
    {
	char_u	*retval = NULL;

	if(set_string_copy(((FunctionObject *) (obj))->name, tv, raise) == -1)
	    return -1;

	tv->v_type = VAR_FUNC;
    }
    else if(PyString_Check(obj))
    {
	char_u	*retval = NULL;
	char_u	*result = (char_u *) PyString_AsString(obj);

	if(result == NULL)
	    return -1;

	if(set_string_copy(result, tv, raise) == -1)
	    return -1;

	tv->v_type = VAR_STRING;
    }
    else if(PyInt_Check(obj))
    {
	tv->v_type = VAR_NUMBER;
	tv->vval.v_number = (varnumber_T) PyInt_AsLong(obj);
    }
    else if(PyDict_Check(obj))
    {
	dict_T	*d;
	char_u	*key;
	dictitem_T	*di;
	PyObject	*lst;
	PyObject	*litem;
	PyObject	*lobj;
	Py_ssize_t	lsize;
	typval_T	v;

	d = dict_alloc();
	if(d == NULL)
	{
	    PyErr_NoMemory();
	    return -1;
	}
	lst = PyDict_Items(obj);
	lsize = PyList_Size(lst);
	while(lsize--)
	{
	    litem = PyList_GetItem(lst, lsize);
	    OBJ_NULL_ERR(litem, "internal error: no dict item")

	    lobj = PyTuple_GetItem(litem, 0);
	    OBJ_NULL_ERR(lobj, "internal error: no key")
	    if(!PyString_Check(lobj))
	    {
		if(raise)
		    PyErr_SetString(PyExc_TypeError, _("key is not a string"));
		return -1;
	    }
	    key = (char_u *) PyString_AsString(lobj);

	    lobj = PyTuple_GetItem(litem, 1);
	    OBJ_NULL_ERR(lobj, "internal error: no value")

	    di = dictitem_alloc(key);
	    if(di == NULL)
	    {
		if(raise)
		    PyErr_NoMemory();
		return -1;
	    }
	    if(ConvertFromPyObject(lobj, &v, raise) == -1)
	    {
		vim_free(di);
		return -1;
	    }
	    if(dict_add(d, di) == FAIL)
	    {
		vim_free(di);
		if(raise)
		    PyErr_SetVim(_("failed to add key to dictionary"));
		return -1;
	    }
	    copy_tv(&v, &di->di_tv);
	}

	tv->v_type = VAR_DICT;
	tv->vval.v_dict = d;
    }
    else if(PyList_Check(obj))
    {
	list_T	*l;

	l = list_alloc();
	if(list_py_concat(l, obj, PyList_Size, PyList_GetItem, raise)==-1)
	    return -1;

	tv->v_type = VAR_LIST;
	tv->vval.v_list = l;
    }
    else if(PyTuple_Check(obj))
    {
	list_T	*l;

	l = list_alloc();
	if(list_py_concat(l, obj, PyTuple_Size, PyTuple_GetItem, raise)==-1)
	    return -1;

	tv->v_type = VAR_LIST;
	tv->vval.v_list = l;
    }
#ifdef FEAT_FLOAT
    else if(PyFloat_Check(obj))
    {
	tv->v_type = VAR_FLOAT;
	tv->vval.v_float = (float_T) PyFloat_AsDouble(obj);
    }
#endif
    else {
	if(raise)
	    PyErr_SetString(PyExc_TypeError, _("unable to convert to vim structure"));
	return -1;
    }
    return 0;
}

    void
do_pyeval (char_u *str, typval_T *rettv)
{
    PyObject *r = (PyObject *) DoPythonCommand(NULL, (char *) str, 1);
    if(r == NULL)
    {
	EMSG(_("E858: Eval did not return a valid python object"));
	return;
    }
    if(ConvertFromPyObject(r, rettv, 0) == -1)
    {
	EMSG(_("E859: Failed to convert returned python object to vim value"));
	return;
    }
    switch(rettv->v_type)
    {
	case VAR_DICT: ++rettv->vval.v_dict->dv_refcount; break;
	case VAR_LIST: ++rettv->vval.v_list->lv_refcount; break;
	case VAR_FUNC: func_ref(rettv->vval.v_string);    break;
    }
}

/* Don't generate a prototype for the next function, it generates an error on
 * newer Python versions. */
#if PYTHON_API_VERSION < 1007 /* Python 1.4 */ && !defined(PROTO)

    char *
Py_GetProgramName(void)
{
    return "vim";
}
#endif /* Python 1.4 */

/* FIXME Following three functions are copy-paste from if_lua, with 
 * modification: setting ?v_copyID is done in set_ref_in_(dict|list) */
static void set_ref_in_tv(typval_T *tv, int copyID);

    static void
set_ref_in_dict(dict_T *d, int copyID)
{
    hashtab_T *ht = &d->dv_hashtab;
    int n = ht->ht_used;
    hashitem_T *hi;
    d->dv_copyID = copyID;
    for (hi = ht->ht_array; n > 0; ++hi)
	if (!HASHITEM_EMPTY(hi))
	{
	    dictitem_T *di = dict_lookup(hi);
	    set_ref_in_tv(&di->di_tv, copyID);
	    --n;
	}
}

    static void
set_ref_in_list(list_T *l, int copyID)
{
    listitem_T *li;
    l->lv_copyID = copyID;
    for (li = l->lv_first; li != NULL; li = li->li_next)
	set_ref_in_tv(&li->li_tv, copyID);
}

    static void
set_ref_in_tv(typval_T *tv, int copyID)
{
    if (tv->v_type == VAR_LIST)
    {
	list_T *l = tv->vval.v_list;
	if (l != NULL && l->lv_copyID != copyID)
	    set_ref_in_list(l, copyID);
    }
    else if (tv->v_type == VAR_DICT)
    {
	dict_T *d = tv->vval.v_dict;
	if (d != NULL && d->dv_copyID != copyID)
	    set_ref_in_dict(d, copyID);
    }
}

    void
set_ref_in_python (int copyID)
{
    size_t	i = 0;

    if(!initialised)
	return;

    for(i = 0; i <= dictrefs.pht_mask ; i++)
	if(dictrefs.pht_array[i] != NULL && dictrefs.pht_vals[i]->ob_refcnt>0)
	    set_ref_in_dict((dict_T *) dictrefs.pht_array[i], copyID);

    for(i = 0; i <= listrefs.pht_mask ; i++)
	if(listrefs.pht_array[i] != NULL && listrefs.pht_vals[i]->ob_refcnt>0)
	    set_ref_in_list((list_T *) listrefs.pht_array[i], copyID);
}

    static void
init_structs(void)
{
    vim_memset(&OutputType, 0, sizeof(OutputType));
    OutputType.tp_name = "message";
    OutputType.tp_basicsize = sizeof(OutputObject);
    OutputType.tp_getattr = OutputGetattr;
    OutputType.tp_setattr = OutputSetattr;

    vim_memset(&RangeType, 0, sizeof(RangeType));
    RangeType.tp_name = "range";
    RangeType.tp_basicsize = sizeof(RangeObject);
    RangeType.tp_dealloc = RangeDestructor;
    RangeType.tp_getattr = RangeGetattr;
    RangeType.tp_repr = RangeRepr;
    RangeType.tp_as_sequence = &RangeAsSeq;
}
