/* vi:set ts=8 sts=4 sw=4:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */
/*
 * Python extensions by Paul Moore, David Leonard, Roland Puntaier.
 *
 * Common code for if_python.c and if_python3.c.
 */

#ifdef FEAT_MBYTE
# define ENC_OPT p_enc
#else
# define ENC_OPT "latin1"
#endif

/*
 * obtain a lock on the Vim data structures
 */
    static void
Python_Lock_Vim(void)
{
}

/*
 * release a lock on the Vim data structures
 */
    static void
Python_Release_Vim(void)
{
}

/* Output object definition
 */

static PyObject *OutputWrite(PyObject *, PyObject *);
static PyObject *OutputWritelines(PyObject *, PyObject *);
static PyObject *OutputFlush(PyObject *, PyObject *);

/* Function to write a line, points to either msg() or emsg(). */
typedef void (*writefn)(char_u *);
static void writer(writefn fn, char_u *str, PyInt n);

typedef struct
{
    PyObject_HEAD
    long softspace;
    long error;
} OutputObject;

static struct PyMethodDef OutputMethods[] = {
    /* name,	    function,		calling,    documentation */
    {"write",	    OutputWrite,	1,	    ""},
    {"writelines",  OutputWritelines,	1,	    ""},
    {"flush",       OutputFlush,        1,          ""},
    { NULL,	    NULL,		0,	    NULL}
};

#define PyErr_SetVim(str) PyErr_SetString(VimError, str)

/*************/

/* Output buffer management
 */

    static PyObject *
OutputWrite(PyObject *self, PyObject *args)
{
    int len;
    char *str = NULL;
    int error = ((OutputObject *)(self))->error;

    if (!PyArg_ParseTuple(args, "et#", ENC_OPT, &str, &len))
	return NULL;

    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();
    writer((writefn)(error ? emsg : msg), (char_u *)str, len);
    Python_Release_Vim();
    Py_END_ALLOW_THREADS
    PyMem_Free(str);

    Py_INCREF(Py_None);
    return Py_None;
}

    static PyObject *
OutputWritelines(PyObject *self, PyObject *args)
{
    PyInt n;
    PyInt i;
    PyObject *list;
    int error = ((OutputObject *)(self))->error;

    if (!PyArg_ParseTuple(args, "O", &list))
	return NULL;
    Py_INCREF(list);

    if (!PyList_Check(list))
    {
	PyErr_SetString(PyExc_TypeError, _("writelines() requires list of strings"));
	Py_DECREF(list);
	return NULL;
    }

    n = PyList_Size(list);

    for (i = 0; i < n; ++i)
    {
	PyObject *line = PyList_GetItem(list, i);
	char *str = NULL;
	PyInt len;

	if (!PyArg_Parse(line, "et#", ENC_OPT, &str, &len))
	{
	    PyErr_SetString(PyExc_TypeError, _("writelines() requires list of strings"));
	    Py_DECREF(list);
	    return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	Python_Lock_Vim();
	writer((writefn)(error ? emsg : msg), (char_u *)str, len);
	Python_Release_Vim();
	Py_END_ALLOW_THREADS
	PyMem_Free(str);
    }

    Py_DECREF(list);
    Py_INCREF(Py_None);
    return Py_None;
}

    static PyObject *
OutputFlush(PyObject *self UNUSED, PyObject *args UNUSED)
{
    /* do nothing */
    Py_INCREF(Py_None);
    return Py_None;
}


/* Buffer IO, we write one whole line at a time. */
static garray_T io_ga = {0, 0, 1, 80, NULL};
static writefn old_fn = NULL;

    static void
PythonIO_Flush(void)
{
    if (old_fn != NULL && io_ga.ga_len > 0)
    {
	((char_u *)io_ga.ga_data)[io_ga.ga_len] = NUL;
	old_fn((char_u *)io_ga.ga_data);
    }
    io_ga.ga_len = 0;
}

    static void
writer(writefn fn, char_u *str, PyInt n)
{
    char_u *ptr;

    /* Flush when switching output function. */
    if (fn != old_fn)
	PythonIO_Flush();
    old_fn = fn;

    /* Write each NL separated line.  Text after the last NL is kept for
     * writing later. */
    while (n > 0 && (ptr = memchr(str, '\n', n)) != NULL)
    {
	PyInt len = ptr - str;

	if (ga_grow(&io_ga, (int)(len + 1)) == FAIL)
	    break;

	mch_memmove(((char *)io_ga.ga_data) + io_ga.ga_len, str, (size_t)len);
	((char *)io_ga.ga_data)[io_ga.ga_len + len] = NUL;
	fn((char_u *)io_ga.ga_data);
	str = ptr + 1;
	n -= len + 1;
	io_ga.ga_len = 0;
    }

    /* Put the remaining text into io_ga for later printing. */
    if (n > 0 && ga_grow(&io_ga, (int)(n + 1)) == OK)
    {
	mch_memmove(((char *)io_ga.ga_data) + io_ga.ga_len, str, (size_t)n);
	io_ga.ga_len += (int)n;
    }
}

/***************/

static PyTypeObject OutputType;

static OutputObject Output =
{
    PyObject_HEAD_INIT(&OutputType)
    0,
    0
};

static OutputObject Error =
{
    PyObject_HEAD_INIT(&OutputType)
    0,
    1
};

    static int
PythonIO_Init_io(void)
{
    PySys_SetObject("stdout", (PyObject *)(void *)&Output);
    PySys_SetObject("stderr", (PyObject *)(void *)&Error);

    if (PyErr_Occurred())
    {
	EMSG(_("E264: Python: Error initialising I/O objects"));
	return -1;
    }

    return 0;
}


static PyObject *VimError;

/* Check to see whether a Vim error has been reported, or a keyboard
 * interrupt has been detected.
 */
    static int
VimErrorCheck(void)
{
    if (got_int)
    {
	PyErr_SetNone(PyExc_KeyboardInterrupt);
	return 1;
    }
    else if (did_emsg && !PyErr_Occurred())
    {
	PyErr_SetNone(VimError);
	return 1;
    }

    return 0;
}

/* Vim module - Implementation
 */
    static PyObject *
VimCommand(PyObject *self UNUSED, PyObject *args)
{
    char *cmd;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "s", &cmd))
	return NULL;

    PyErr_Clear();

    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();

    do_cmdline_cmd((char_u *)cmd);
    update_screen(VALID);

    Python_Release_Vim();
    Py_END_ALLOW_THREADS

    if (VimErrorCheck())
	result = NULL;
    else
	result = Py_None;

    Py_XINCREF(result);
    return result;
}

#ifdef FEAT_EVAL
/*
 * Function to translate a typval_T into a PyObject; this will recursively
 * translate lists/dictionaries into their Python equivalents.
 *
 * The depth parameter is to avoid infinite recursion, set it to 1 when
 * you call VimToPython.
 */
    static PyObject *
VimToPython(typval_T *our_tv, int depth, PyObject *lookupDict)
{
    PyObject	*result;
    PyObject	*newObj;
    char	ptrBuf[NUMBUFLEN];

    /* Avoid infinite recursion */
    if (depth > 100)
    {
	Py_INCREF(Py_None);
	result = Py_None;
	return result;
    }

    /* Check if we run into a recursive loop.  The item must be in lookupDict
     * then and we can use it again. */
    if ((our_tv->v_type == VAR_LIST && our_tv->vval.v_list != NULL)
	    || (our_tv->v_type == VAR_DICT && our_tv->vval.v_dict != NULL))
    {
	sprintf(ptrBuf, PRINTF_DECIMAL_LONG_U,
		our_tv->v_type == VAR_LIST ? (long_u)our_tv->vval.v_list
					   : (long_u)our_tv->vval.v_dict);
	result = PyDict_GetItemString(lookupDict, ptrBuf);
	if (result != NULL)
	{
	    Py_INCREF(result);
	    return result;
	}
    }

    if (our_tv->v_type == VAR_STRING)
    {
	result = Py_BuildValue("s", our_tv->vval.v_string);
    }
    else if (our_tv->v_type == VAR_NUMBER)
    {
	char buf[NUMBUFLEN];

	/* For backwards compatibility numbers are stored as strings. */
	sprintf(buf, "%ld", (long)our_tv->vval.v_number);
	result = Py_BuildValue("s", buf);
    }
# ifdef FEAT_FLOAT
    else if (our_tv->v_type == VAR_FLOAT)
    {
	char buf[NUMBUFLEN];

	sprintf(buf, "%f", our_tv->vval.v_float);
	result = Py_BuildValue("s", buf);
    }
# endif
    else if (our_tv->v_type == VAR_LIST)
    {
	list_T		*list = our_tv->vval.v_list;
	listitem_T	*curr;

	result = PyList_New(0);

	if (list != NULL)
	{
	    PyDict_SetItemString(lookupDict, ptrBuf, result);

	    for (curr = list->lv_first; curr != NULL; curr = curr->li_next)
	    {
		newObj = VimToPython(&curr->li_tv, depth + 1, lookupDict);
		PyList_Append(result, newObj);
		Py_DECREF(newObj);
	    }
	}
    }
    else if (our_tv->v_type == VAR_DICT)
    {
	result = PyDict_New();

	if (our_tv->vval.v_dict != NULL)
	{
	    hashtab_T	*ht = &our_tv->vval.v_dict->dv_hashtab;
	    long_u	todo = ht->ht_used;
	    hashitem_T	*hi;
	    dictitem_T	*di;

	    PyDict_SetItemString(lookupDict, ptrBuf, result);

	    for (hi = ht->ht_array; todo > 0; ++hi)
	    {
		if (!HASHITEM_EMPTY(hi))
		{
		    --todo;

		    di = dict_lookup(hi);
		    newObj = VimToPython(&di->di_tv, depth + 1, lookupDict);
		    PyDict_SetItemString(result, (char *)hi->hi_key, newObj);
		    Py_DECREF(newObj);
		}
	    }
	}
    }
    else
    {
	Py_INCREF(Py_None);
	result = Py_None;
    }

    return result;
}
#endif

    static PyObject *
VimEval(PyObject *self UNUSED, PyObject *args)
{
#ifdef FEAT_EVAL
    char	*expr;
    typval_T	*our_tv;
    PyObject	*result;
    PyObject    *lookup_dict;

    if (!PyArg_ParseTuple(args, "s", &expr))
	return NULL;

    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();
    our_tv = eval_expr((char_u *)expr, NULL);

    Python_Release_Vim();
    Py_END_ALLOW_THREADS

    if (our_tv == NULL)
    {
	PyErr_SetVim(_("invalid expression"));
	return NULL;
    }

    /* Convert the Vim type into a Python type.  Create a dictionary that's
     * used to check for recursive loops. */
    lookup_dict = PyDict_New();
    result = VimToPython(our_tv, 1, lookup_dict);
    Py_DECREF(lookup_dict);


    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();
    free_tv(our_tv);
    Python_Release_Vim();
    Py_END_ALLOW_THREADS

    return result;
#else
    PyErr_SetVim(_("expressions disabled at compile time"));
    return NULL;
#endif
}

static PyObject *ConvertToPyObject(typval_T *);

    static PyObject *
VimEvalPy(PyObject *self UNUSED, PyObject *args)
{
#ifdef FEAT_EVAL
    char	*expr;
    typval_T	*our_tv;
    PyObject	*result;
    PyObject    *lookup_dict;

    if (!PyArg_ParseTuple(args, "s", &expr))
	return NULL;

    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();
    our_tv = eval_expr((char_u *)expr, NULL);

    Python_Release_Vim();
    Py_END_ALLOW_THREADS

    if (our_tv == NULL)
    {
	PyErr_SetVim(_("invalid expression"));
	return NULL;
    }

    result = ConvertToPyObject(our_tv);
    Py_BEGIN_ALLOW_THREADS
    Python_Lock_Vim();
    free_tv(our_tv);
    Python_Release_Vim();
    Py_END_ALLOW_THREADS

    return result;
#else
    PyErr_SetVim(_("expressions disabled at compile time"));
    return NULL;
#endif
}

/*
 * Vim module - Definitions
 */

static struct PyMethodDef VimMethods[] = {
    /* name,	     function,		calling,    documentation */
    {"command",	     VimCommand,	1,	    "Execute a Vim ex-mode command" },
    {"eval",	     VimEval,		1,	    "Evaluate an expression using Vim evaluator" },
    {"bindeval",     VimEvalPy,         1,          "Like eval(), but returns objects attached to vim ones"},
    { NULL,	     NULL,		0,	    NULL }
};

typedef struct
{
    PyObject_HEAD
    buf_T *buf;
} BufferObject;

#define INVALID_BUFFER_VALUE ((buf_T *)(-1))

/*
 * Buffer list object - Implementation
 */

    static PyInt
BufListLength(PyObject *self UNUSED)
{
    buf_T	*b = firstbuf;
    PyInt	n = 0;

    while (b)
    {
	++n;
	b = b->b_next;
    }

    return n;
}

    static PyObject *
BufListItem(PyObject *self UNUSED, PyInt n)
{
    buf_T *b;

    for (b = firstbuf; b; b = b->b_next, --n)
    {
	if (n == 0)
	    return BufferNew(b);
    }

    PyErr_SetString(PyExc_IndexError, _("no such buffer"));
    return NULL;
}

typedef struct
{
    PyObject_HEAD
    win_T	*win;
} WindowObject;

static int ConvertFromPyObject(PyObject *, typval_T *);

typedef struct
{
    PyObject_HEAD
    dict_T	*dict;
} DictionaryObject;

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
    PyObject	*bytes = NULL;

    DICTKEY_GET(NULL)

    val = dict_find(((DictionaryObject *) (self))->dict, key, -1);

    DICTKEY_UNREF

    return ConvertToPyObject(&val->di_tv);
}

    static PyInt
DictionaryAssItem(PyObject *self, PyObject *keyObject, PyObject *valObject)
{
    char_u	*key;
    typval_T	tv;
    dict_T	*d = ((DictionaryObject *)(self))->dict;
    dictitem_T	*di;
    PyObject	*bytes = NULL;

    if(d->dv_lock)
    {
	PyErr_SetVim(_("dict is locked"));
	return -1;
    }

    DICTKEY_GET(-1)

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

    if(ConvertFromPyObject(valObject, &tv) == -1)
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

    DICTKEY_UNREF

    copy_tv(&tv, &di->di_tv);
    return 0;
}

typedef struct
{
    PyObject_HEAD
    list_T	*list;
} ListObject;

    static int
list_py_concat(list_T *l, PyObject *obj, lenfunc Size, ssizeargfunc Item)
{
    Py_ssize_t	i;
    Py_ssize_t	lsize = Size(obj);
    PyObject	*litem;
    typval_T	v;

    for(i=0; i<lsize; i++)
    {
	litem = Item(obj, i);
	if(obj == NULL)
	{
	    if(raise)
		PyErr_SetVim(_("internal error: no list item"));
	    return -1;
	}
	if(ConvertFromPyObject(litem, &v) == -1)
	    return -1;

	if(list_append_tv(l, &v) == FAIL)
	{
	    PyErr_SetVim(_("failed to add item to list"));
	    return -1;
	}
    }
    return 0;
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
	list_remove(l, li, li);
	clear_tv(&li->li_tv);
	vim_free(li);
	return 0;
    }

    if(ConvertFromPyObject(obj, &tv) == -1)
	return -1;

    if(index == length)
    {
	if(list_append_tv(l, &tv) == FAIL)
	{
	    PyErr_SetVim(_("internal error: failed to add item to list"));
	    return -1;
	}
    }
    else
    {
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
    else
    {
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
	if(ConvertFromPyObject(litem, &v) == -1)
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

    if(list_py_concat(l, obj, PyList_Size, PyList_GetItem)==-1)
	return NULL;

    Py_INCREF(self);

    return self;
}

static struct PyMethodDef ListMethods[] = {
    {"extend", (PyCFunction)ListConcatInPlace, METH_O, ""},
    { NULL,	    NULL,		0,	    NULL }
};

typedef struct
{
    PyObject_HEAD
    char_u	*name;
} FunctionObject;

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

    if(ConvertFromPyObject(argsObject, &args) == -1)
	return NULL;

    if(kwargs != NULL)
    {
	selfdictObject = PyDict_GetItemString(kwargs, "self");
	if(selfdictObject != NULL)
	{
	    if(!PyDict_Check(selfdictObject))
	    {
		PyErr_SetString(PyExc_TypeError, _("'self' argument must be a dictionary"));
		clear_tv(&args);
		return NULL;
	    }
	    if(ConvertFromPyObject(selfdictObject, &selfdicttv) == -1)
		return NULL;
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

static struct PyMethodDef FunctionMethods[] = {
    {"__call__",    (PyCFunction)FunctionCall, METH_VARARGS|METH_KEYWORDS, ""},
    { NULL,	    NULL,		0,	    NULL }
};

#define INVALID_WINDOW_VALUE ((win_T *)(-1))

    static int
CheckWindow(WindowObject *this)
{
    if (this->win == INVALID_WINDOW_VALUE)
    {
	PyErr_SetVim(_("attempt to refer to deleted window"));
	return -1;
    }

    return 0;
}

static int WindowSetattr(PyObject *, char *, PyObject *);
static PyObject *WindowRepr(PyObject *);

    static int
WindowSetattr(PyObject *self, char *name, PyObject *val)
{
    WindowObject *this = (WindowObject *)(self);

    if (CheckWindow(this))
	return -1;

    if (strcmp(name, "buffer") == 0)
    {
	PyErr_SetString(PyExc_TypeError, _("readonly attribute"));
	return -1;
    }
    else if (strcmp(name, "cursor") == 0)
    {
	long lnum;
	long col;

	if (!PyArg_Parse(val, "(ll)", &lnum, &col))
	    return -1;

	if (lnum <= 0 || lnum > this->win->w_buffer->b_ml.ml_line_count)
	{
	    PyErr_SetVim(_("cursor position outside buffer"));
	    return -1;
	}

	/* Check for keyboard interrupts */
	if (VimErrorCheck())
	    return -1;

	this->win->w_cursor.lnum = lnum;
	this->win->w_cursor.col = col;
#ifdef FEAT_VIRTUALEDIT
	this->win->w_cursor.coladd = 0;
#endif
	/* When column is out of range silently correct it. */
	check_cursor_col_win(this->win);

	update_screen(VALID);
	return 0;
    }
    else if (strcmp(name, "height") == 0)
    {
	int	height;
	win_T	*savewin;

	if (!PyArg_Parse(val, "i", &height))
	    return -1;

#ifdef FEAT_GUI
	need_mouse_correct = TRUE;
#endif
	savewin = curwin;
	curwin = this->win;
	win_setheight(height);
	curwin = savewin;

	/* Check for keyboard interrupts */
	if (VimErrorCheck())
	    return -1;

	return 0;
    }
#ifdef FEAT_VERTSPLIT
    else if (strcmp(name, "width") == 0)
    {
	int	width;
	win_T	*savewin;

	if (!PyArg_Parse(val, "i", &width))
	    return -1;

#ifdef FEAT_GUI
	need_mouse_correct = TRUE;
#endif
	savewin = curwin;
	curwin = this->win;
	win_setwidth(width);
	curwin = savewin;

	/* Check for keyboard interrupts */
	if (VimErrorCheck())
	    return -1;

	return 0;
    }
#endif
    else
    {
	PyErr_SetString(PyExc_AttributeError, name);
	return -1;
    }
}

    static PyObject *
WindowRepr(PyObject *self)
{
    static char repr[100];
    WindowObject *this = (WindowObject *)(self);

    if (this->win == INVALID_WINDOW_VALUE)
    {
	vim_snprintf(repr, 100, _("<window object (deleted) at %p>"), (self));
	return PyString_FromString(repr);
    }
    else
    {
	int	i = 0;
	win_T	*w;

	for (w = firstwin; w != NULL && w != this->win; w = W_NEXT(w))
	    ++i;

	if (w == NULL)
	    vim_snprintf(repr, 100, _("<window object (unknown) at %p>"),
								      (self));
	else
	    vim_snprintf(repr, 100, _("<window %d>"), i);

	return PyString_FromString(repr);
    }
}

/*
 * Window list object - Implementation
 */
    static PyInt
WinListLength(PyObject *self UNUSED)
{
    win_T	*w = firstwin;
    PyInt	n = 0;

    while (w != NULL)
    {
	++n;
	w = W_NEXT(w);
    }

    return n;
}

    static PyObject *
WinListItem(PyObject *self UNUSED, PyInt n)
{
    win_T *w;

    for (w = firstwin; w != NULL; w = W_NEXT(w), --n)
	if (n == 0)
	    return WindowNew(w);

    PyErr_SetString(PyExc_IndexError, _("no such window"));
    return NULL;
}

/* Convert a Python string into a Vim line.
 *
 * The result is in allocated memory. All internal nulls are replaced by
 * newline characters. It is an error for the string to contain newline
 * characters.
 *
 * On errors, the Python exception data is set, and NULL is returned.
 */
    static char *
StringToLine(PyObject *obj)
{
    const char *str;
    char *save;
    PyObject *bytes;
    PyInt len;
    PyInt i;
    char *p;

    if (obj == NULL || !PyString_Check(obj))
    {
	PyErr_BadArgument();
	return NULL;
    }

    bytes = PyString_AsBytes(obj);  /* for Python 2 this does nothing */
    str = PyString_AsString(bytes);
    len = PyString_Size(bytes);

    /*
     * Error checking: String must not contain newlines, as we
     * are replacing a single line, and we must replace it with
     * a single line.
     * A trailing newline is removed, so that append(f.readlines()) works.
     */
    p = memchr(str, '\n', len);
    if (p != NULL)
    {
	if (p == str + len - 1)
	    --len;
	else
	{
	    PyErr_SetVim(_("string cannot contain newlines"));
	    return NULL;
	}
    }

    /* Create a copy of the string, with internal nulls replaced by
     * newline characters, as is the vim convention.
     */
    save = (char *)alloc((unsigned)(len+1));
    if (save == NULL)
    {
	PyErr_NoMemory();
	return NULL;
    }

    for (i = 0; i < len; ++i)
    {
	if (str[i] == '\0')
	    save[i] = '\n';
	else
	    save[i] = str[i];
    }

    save[i] = '\0';
    PyString_FreeBytes(bytes);  /* Python 2 does nothing here */

    return save;
}

/* Get a line from the specified buffer. The line number is
 * in Vim format (1-based). The line is returned as a Python
 * string object.
 */
    static PyObject *
GetBufferLine(buf_T *buf, PyInt n)
{
    return LineToString((char *)ml_get_buf(buf, (linenr_T)n, FALSE));
}


/* Get a list of lines from the specified buffer. The line numbers
 * are in Vim format (1-based). The range is from lo up to, but not
 * including, hi. The list is returned as a Python list of string objects.
 */
    static PyObject *
GetBufferLineList(buf_T *buf, PyInt lo, PyInt hi)
{
    PyInt i;
    PyInt n = hi - lo;
    PyObject *list = PyList_New(n);

    if (list == NULL)
	return NULL;

    for (i = 0; i < n; ++i)
    {
	PyObject *str = LineToString((char *)ml_get_buf(buf, (linenr_T)(lo+i), FALSE));

	/* Error check - was the Python string creation OK? */
	if (str == NULL)
	{
	    Py_DECREF(list);
	    return NULL;
	}

	/* Set the list item */
	if (PyList_SetItem(list, i, str))
	{
	    Py_DECREF(str);
	    Py_DECREF(list);
	    return NULL;
	}
    }

    /* The ownership of the Python list is passed to the caller (ie,
     * the caller should Py_DECREF() the object when it is finished
     * with it).
     */

    return list;
}

/*
 * Check if deleting lines made the cursor position invalid.
 * Changed the lines from "lo" to "hi" and added "extra" lines (negative if
 * deleted).
 */
    static void
py_fix_cursor(linenr_T lo, linenr_T hi, linenr_T extra)
{
    if (curwin->w_cursor.lnum >= lo)
    {
	/* Adjust the cursor position if it's in/after the changed
	 * lines. */
	if (curwin->w_cursor.lnum >= hi)
	{
	    curwin->w_cursor.lnum += extra;
	    check_cursor_col();
	}
	else if (extra < 0)
	{
	    curwin->w_cursor.lnum = lo;
	    check_cursor();
	}
	else
	    check_cursor_col();
	changed_cline_bef_curs();
    }
    invalidate_botline();
}

/*
 * Replace a line in the specified buffer. The line number is
 * in Vim format (1-based). The replacement line is given as
 * a Python string object. The object is checked for validity
 * and correct format. Errors are returned as a value of FAIL.
 * The return value is OK on success.
 * If OK is returned and len_change is not NULL, *len_change
 * is set to the change in the buffer length.
 */
    static int
SetBufferLine(buf_T *buf, PyInt n, PyObject *line, PyInt *len_change)
{
    /* First of all, we check the thpe of the supplied Python object.
     * There are three cases:
     *	  1. NULL, or None - this is a deletion.
     *	  2. A string	   - this is a replacement.
     *	  3. Anything else - this is an error.
     */
    if (line == Py_None || line == NULL)
    {
	buf_T *savebuf = curbuf;

	PyErr_Clear();
	curbuf = buf;

	if (u_savedel((linenr_T)n, 1L) == FAIL)
	    PyErr_SetVim(_("cannot save undo information"));
	else if (ml_delete((linenr_T)n, FALSE) == FAIL)
	    PyErr_SetVim(_("cannot delete line"));
	else
	{
	    if (buf == curwin->w_buffer)
		py_fix_cursor((linenr_T)n, (linenr_T)n + 1, (linenr_T)-1);
	    deleted_lines_mark((linenr_T)n, 1L);
	}

	curbuf = savebuf;

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = -1;

	return OK;
    }
    else if (PyString_Check(line))
    {
	char *save = StringToLine(line);
	buf_T *savebuf = curbuf;

	if (save == NULL)
	    return FAIL;

	/* We do not need to free "save" if ml_replace() consumes it. */
	PyErr_Clear();
	curbuf = buf;

	if (u_savesub((linenr_T)n) == FAIL)
	{
	    PyErr_SetVim(_("cannot save undo information"));
	    vim_free(save);
	}
	else if (ml_replace((linenr_T)n, (char_u *)save, FALSE) == FAIL)
	{
	    PyErr_SetVim(_("cannot replace line"));
	    vim_free(save);
	}
	else
	    changed_bytes((linenr_T)n, 0);

	curbuf = savebuf;

	/* Check that the cursor is not beyond the end of the line now. */
	if (buf == curwin->w_buffer)
	    check_cursor_col();

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = 0;

	return OK;
    }
    else
    {
	PyErr_BadArgument();
	return FAIL;
    }
}

/* Replace a range of lines in the specified buffer. The line numbers are in
 * Vim format (1-based). The range is from lo up to, but not including, hi.
 * The replacement lines are given as a Python list of string objects. The
 * list is checked for validity and correct format. Errors are returned as a
 * value of FAIL.  The return value is OK on success.
 * If OK is returned and len_change is not NULL, *len_change
 * is set to the change in the buffer length.
 */
    static int
SetBufferLineList(buf_T *buf, PyInt lo, PyInt hi, PyObject *list, PyInt *len_change)
{
    /* First of all, we check the thpe of the supplied Python object.
     * There are three cases:
     *	  1. NULL, or None - this is a deletion.
     *	  2. A list	   - this is a replacement.
     *	  3. Anything else - this is an error.
     */
    if (list == Py_None || list == NULL)
    {
	PyInt	i;
	PyInt	n = (int)(hi - lo);
	buf_T	*savebuf = curbuf;

	PyErr_Clear();
	curbuf = buf;

	if (u_savedel((linenr_T)lo, (long)n) == FAIL)
	    PyErr_SetVim(_("cannot save undo information"));
	else
	{
	    for (i = 0; i < n; ++i)
	    {
		if (ml_delete((linenr_T)lo, FALSE) == FAIL)
		{
		    PyErr_SetVim(_("cannot delete line"));
		    break;
		}
	    }
	    if (buf == curwin->w_buffer)
		py_fix_cursor((linenr_T)lo, (linenr_T)hi, (linenr_T)-n);
	    deleted_lines_mark((linenr_T)lo, (long)i);
	}

	curbuf = savebuf;

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = -n;

	return OK;
    }
    else if (PyList_Check(list))
    {
	PyInt	i;
	PyInt	new_len = PyList_Size(list);
	PyInt	old_len = hi - lo;
	PyInt	extra = 0;	/* lines added to text, can be negative */
	char	**array;
	buf_T	*savebuf;

	if (new_len == 0)	/* avoid allocating zero bytes */
	    array = NULL;
	else
	{
	    array = (char **)alloc((unsigned)(new_len * sizeof(char *)));
	    if (array == NULL)
	    {
		PyErr_NoMemory();
		return FAIL;
	    }
	}

	for (i = 0; i < new_len; ++i)
	{
	    PyObject *line = PyList_GetItem(list, i);

	    array[i] = StringToLine(line);
	    if (array[i] == NULL)
	    {
		while (i)
		    vim_free(array[--i]);
		vim_free(array);
		return FAIL;
	    }
	}

	savebuf = curbuf;

	PyErr_Clear();
	curbuf = buf;

	if (u_save((linenr_T)(lo-1), (linenr_T)hi) == FAIL)
	    PyErr_SetVim(_("cannot save undo information"));

	/* If the size of the range is reducing (ie, new_len < old_len) we
	 * need to delete some old_len. We do this at the start, by
	 * repeatedly deleting line "lo".
	 */
	if (!PyErr_Occurred())
	{
	    for (i = 0; i < old_len - new_len; ++i)
		if (ml_delete((linenr_T)lo, FALSE) == FAIL)
		{
		    PyErr_SetVim(_("cannot delete line"));
		    break;
		}
	    extra -= i;
	}

	/* For as long as possible, replace the existing old_len with the
	 * new old_len. This is a more efficient operation, as it requires
	 * less memory allocation and freeing.
	 */
	if (!PyErr_Occurred())
	{
	    for (i = 0; i < old_len && i < new_len; ++i)
		if (ml_replace((linenr_T)(lo+i), (char_u *)array[i], FALSE)
								      == FAIL)
		{
		    PyErr_SetVim(_("cannot replace line"));
		    break;
		}
	}
	else
	    i = 0;

	/* Now we may need to insert the remaining new old_len. If we do, we
	 * must free the strings as we finish with them (we can't pass the
	 * responsibility to vim in this case).
	 */
	if (!PyErr_Occurred())
	{
	    while (i < new_len)
	    {
		if (ml_append((linenr_T)(lo + i - 1),
					(char_u *)array[i], 0, FALSE) == FAIL)
		{
		    PyErr_SetVim(_("cannot insert line"));
		    break;
		}
		vim_free(array[i]);
		++i;
		++extra;
	    }
	}

	/* Free any left-over old_len, as a result of an error */
	while (i < new_len)
	{
	    vim_free(array[i]);
	    ++i;
	}

	/* Free the array of old_len. All of its contents have now
	 * been dealt with (either freed, or the responsibility passed
	 * to vim.
	 */
	vim_free(array);

	/* Adjust marks. Invalidate any which lie in the
	 * changed range, and move any in the remainder of the buffer.
	 */
	mark_adjust((linenr_T)lo, (linenr_T)(hi - 1),
						  (long)MAXLNUM, (long)extra);
	changed_lines((linenr_T)lo, 0, (linenr_T)hi, (long)extra);

	if (buf == curwin->w_buffer)
	    py_fix_cursor((linenr_T)lo, (linenr_T)hi, (linenr_T)extra);

	curbuf = savebuf;

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = new_len - old_len;

	return OK;
    }
    else
    {
	PyErr_BadArgument();
	return FAIL;
    }
}

/* Insert a number of lines into the specified buffer after the specifed line.
 * The line number is in Vim format (1-based). The lines to be inserted are
 * given as a Python list of string objects or as a single string. The lines
 * to be added are checked for validity and correct format. Errors are
 * returned as a value of FAIL.  The return value is OK on success.
 * If OK is returned and len_change is not NULL, *len_change
 * is set to the change in the buffer length.
 */
    static int
InsertBufferLines(buf_T *buf, PyInt n, PyObject *lines, PyInt *len_change)
{
    /* First of all, we check the type of the supplied Python object.
     * It must be a string or a list, or the call is in error.
     */
    if (PyString_Check(lines))
    {
	char	*str = StringToLine(lines);
	buf_T	*savebuf;

	if (str == NULL)
	    return FAIL;

	savebuf = curbuf;

	PyErr_Clear();
	curbuf = buf;

	if (u_save((linenr_T)n, (linenr_T)(n+1)) == FAIL)
	    PyErr_SetVim(_("cannot save undo information"));
	else if (ml_append((linenr_T)n, (char_u *)str, 0, FALSE) == FAIL)
	    PyErr_SetVim(_("cannot insert line"));
	else
	    appended_lines_mark((linenr_T)n, 1L);

	vim_free(str);
	curbuf = savebuf;
	update_screen(VALID);

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = 1;

	return OK;
    }
    else if (PyList_Check(lines))
    {
	PyInt	i;
	PyInt	size = PyList_Size(lines);
	char	**array;
	buf_T	*savebuf;

	array = (char **)alloc((unsigned)(size * sizeof(char *)));
	if (array == NULL)
	{
	    PyErr_NoMemory();
	    return FAIL;
	}

	for (i = 0; i < size; ++i)
	{
	    PyObject *line = PyList_GetItem(lines, i);
	    array[i] = StringToLine(line);

	    if (array[i] == NULL)
	    {
		while (i)
		    vim_free(array[--i]);
		vim_free(array);
		return FAIL;
	    }
	}

	savebuf = curbuf;

	PyErr_Clear();
	curbuf = buf;

	if (u_save((linenr_T)n, (linenr_T)(n + 1)) == FAIL)
	    PyErr_SetVim(_("cannot save undo information"));
	else
	{
	    for (i = 0; i < size; ++i)
	    {
		if (ml_append((linenr_T)(n + i),
					(char_u *)array[i], 0, FALSE) == FAIL)
		{
		    PyErr_SetVim(_("cannot insert line"));

		    /* Free the rest of the lines */
		    while (i < size)
			vim_free(array[i++]);

		    break;
		}
		vim_free(array[i]);
	    }
	    if (i > 0)
		appended_lines_mark((linenr_T)n, (long)i);
	}

	/* Free the array of lines. All of its contents have now
	 * been freed.
	 */
	vim_free(array);

	curbuf = savebuf;
	update_screen(VALID);

	if (PyErr_Occurred() || VimErrorCheck())
	    return FAIL;

	if (len_change)
	    *len_change = size;

	return OK;
    }
    else
    {
	PyErr_BadArgument();
	return FAIL;
    }
}

/*
 * Common routines for buffers and line ranges
 * -------------------------------------------
 */

    static int
CheckBuffer(BufferObject *this)
{
    if (this->buf == INVALID_BUFFER_VALUE)
    {
	PyErr_SetVim(_("attempt to refer to deleted buffer"));
	return -1;
    }

    return 0;
}

    static PyObject *
RBItem(BufferObject *self, PyInt n, PyInt start, PyInt end)
{
    if (CheckBuffer(self))
	return NULL;

    if (n < 0 || n > end - start)
    {
	PyErr_SetString(PyExc_IndexError, _("line number out of range"));
	return NULL;
    }

    return GetBufferLine(self->buf, n+start);
}

    static PyObject *
RBSlice(BufferObject *self, PyInt lo, PyInt hi, PyInt start, PyInt end)
{
    PyInt size;

    if (CheckBuffer(self))
	return NULL;

    size = end - start + 1;

    if (lo < 0)
	lo = 0;
    else if (lo > size)
	lo = size;
    if (hi < 0)
	hi = 0;
    if (hi < lo)
	hi = lo;
    else if (hi > size)
	hi = size;

    return GetBufferLineList(self->buf, lo+start, hi+start);
}

    static PyInt
RBAsItem(BufferObject *self, PyInt n, PyObject *val, PyInt start, PyInt end, PyInt *new_end)
{
    PyInt len_change;

    if (CheckBuffer(self))
	return -1;

    if (n < 0 || n > end - start)
    {
	PyErr_SetString(PyExc_IndexError, _("line number out of range"));
	return -1;
    }

    if (SetBufferLine(self->buf, n+start, val, &len_change) == FAIL)
	return -1;

    if (new_end)
	*new_end = end + len_change;

    return 0;
}

    static PyInt
RBAsSlice(BufferObject *self, PyInt lo, PyInt hi, PyObject *val, PyInt start, PyInt end, PyInt *new_end)
{
    PyInt size;
    PyInt len_change;

    /* Self must be a valid buffer */
    if (CheckBuffer(self))
	return -1;

    /* Sort out the slice range */
    size = end - start + 1;

    if (lo < 0)
	lo = 0;
    else if (lo > size)
	lo = size;
    if (hi < 0)
	hi = 0;
    if (hi < lo)
	hi = lo;
    else if (hi > size)
	hi = size;

    if (SetBufferLineList(self->buf, lo + start, hi + start,
						    val, &len_change) == FAIL)
	return -1;

    if (new_end)
	*new_end = end + len_change;

    return 0;
}


    static PyObject *
RBAppend(BufferObject *self, PyObject *args, PyInt start, PyInt end, PyInt *new_end)
{
    PyObject *lines;
    PyInt len_change;
    PyInt max;
    PyInt n;

    if (CheckBuffer(self))
	return NULL;

    max = n = end - start + 1;

    if (!PyArg_ParseTuple(args, "O|n", &lines, &n))
	return NULL;

    if (n < 0 || n > max)
    {
	PyErr_SetString(PyExc_ValueError, _("line number out of range"));
	return NULL;
    }

    if (InsertBufferLines(self->buf, n + start - 1, lines, &len_change) == FAIL)
	return NULL;

    if (new_end)
	*new_end = end + len_change;

    Py_INCREF(Py_None);
    return Py_None;
}


/* Buffer object - Definitions
 */

typedef struct
{
    PyObject_HEAD
    BufferObject *buf;
    PyInt start;
    PyInt end;
} RangeObject;

    static PyObject *
RangeNew(buf_T *buf, PyInt start, PyInt end)
{
    BufferObject *bufr;
    RangeObject *self;
    self = PyObject_NEW(RangeObject, &RangeType);
    if (self == NULL)
	return NULL;

    bufr = (BufferObject *)BufferNew(buf);
    if (bufr == NULL)
    {
	Py_DECREF(self);
	return NULL;
    }
    Py_INCREF(bufr);

    self->buf = bufr;
    self->start = start;
    self->end = end;

    return (PyObject *)(self);
}

    static PyObject *
BufferAppend(PyObject *self, PyObject *args)
{
    return RBAppend((BufferObject *)(self), args, 1,
		    (PyInt)((BufferObject *)(self))->buf->b_ml.ml_line_count,
		    NULL);
}

    static PyObject *
BufferMark(PyObject *self, PyObject *args)
{
    pos_T	*posp;
    char	*pmark;
    char	mark;
    buf_T	*curbuf_save;

    if (CheckBuffer((BufferObject *)(self)))
	return NULL;

    if (!PyArg_ParseTuple(args, "s", &pmark))
	return NULL;
    mark = *pmark;

    curbuf_save = curbuf;
    curbuf = ((BufferObject *)(self))->buf;
    posp = getmark(mark, FALSE);
    curbuf = curbuf_save;

    if (posp == NULL)
    {
	PyErr_SetVim(_("invalid mark name"));
	return NULL;
    }

    /* Ckeck for keyboard interrupt */
    if (VimErrorCheck())
	return NULL;

    if (posp->lnum <= 0)
    {
	/* Or raise an error? */
	Py_INCREF(Py_None);
	return Py_None;
    }

    return Py_BuildValue("(ll)", (long)(posp->lnum), (long)(posp->col));
}

    static PyObject *
BufferRange(PyObject *self, PyObject *args)
{
    PyInt start;
    PyInt end;

    if (CheckBuffer((BufferObject *)(self)))
	return NULL;

    if (!PyArg_ParseTuple(args, "nn", &start, &end))
	return NULL;

    return RangeNew(((BufferObject *)(self))->buf, start, end);
}

static struct PyMethodDef BufferMethods[] = {
    /* name,	    function,		calling,    documentation */
    {"append",	    BufferAppend,	1,	    "Append data to Vim buffer" },
    {"mark",	    BufferMark,		1,	    "Return (row,col) representing position of named mark" },
    {"range",	    BufferRange,	1,	    "Return a range object which represents the part of the given buffer between line numbers s and e" },
#if PY_VERSION_HEX >= 0x03000000
    {"__dir__",	    BufferDir,		4,	    "List its attributes" },
#endif
    { NULL,	    NULL,		0,	    NULL }
};

    static PyObject *
RangeAppend(PyObject *self, PyObject *args)
{
    return RBAppend(((RangeObject *)(self))->buf, args,
		    ((RangeObject *)(self))->start,
		    ((RangeObject *)(self))->end,
		    &((RangeObject *)(self))->end);
}

    static PyInt
RangeLength(PyObject *self)
{
    /* HOW DO WE SIGNAL AN ERROR FROM THIS FUNCTION? */
    if (CheckBuffer(((RangeObject *)(self))->buf))
	return -1; /* ??? */

    return (((RangeObject *)(self))->end - ((RangeObject *)(self))->start + 1);
}

    static PyObject *
RangeItem(PyObject *self, PyInt n)
{
    return RBItem(((RangeObject *)(self))->buf, n,
		  ((RangeObject *)(self))->start,
		  ((RangeObject *)(self))->end);
}

    static PyObject *
RangeRepr(PyObject *self)
{
    static char repr[100];
    RangeObject *this = (RangeObject *)(self);

    if (this->buf->buf == INVALID_BUFFER_VALUE)
    {
	vim_snprintf(repr, 100, "<range object (for deleted buffer) at %p>",
								      (self));
	return PyString_FromString(repr);
    }
    else
    {
	char *name = (char *)this->buf->buf->b_fname;
	int len;

	if (name == NULL)
	    name = "";
	len = (int)strlen(name);

	if (len > 45)
	    name = name + (45 - len);

	vim_snprintf(repr, 100, "<range %s%s (%d:%d)>",
		len > 45 ? "..." : "", name,
		this->start, this->end);

	return PyString_FromString(repr);
    }
}

    static PyObject *
RangeSlice(PyObject *self, PyInt lo, PyInt hi)
{
    return RBSlice(((RangeObject *)(self))->buf, lo, hi,
		   ((RangeObject *)(self))->start,
		   ((RangeObject *)(self))->end);
}

/*
 * Line range object - Definitions
 */

static struct PyMethodDef RangeMethods[] = {
    /* name,	    function,		calling,    documentation */
    {"append",	    RangeAppend,	1,	    "Append data to the Vim range" },
    { NULL,	    NULL,		0,	    NULL }
};

#define PERTURB_SHIFT 5

typedef struct pyhashtable_S
{
    size_t	pht_mask;	/* mask used for hash value (nr of items in
				 * array is "pht_mask" + 1) */
    size_t	pht_used;	/* number of items used */
    size_t	pht_filled;	/* number of items used + removed */
    int		pht_error;	/* when set growing failed, can't add more
				   items before growing works */
    void	**pht_array;	/* points to the array, allocated when it's
				   not "pht_smallarray" */
    PyObject	**pht_vals;	/* points to the array containing values:
                                   python objects associated with given key */
    void	*pht_smallarray[HT_INIT_SIZE];   /* initial array */
    PyObject	*pht_smallvars[HT_INIT_SIZE];
} pyhashtab_T;

static int pyhash_may_resize __ARGS((pyhashtab_T *ht, size_t minitems));

#define PYOBJ_DELETED(obj) (obj->ob_refcnt<=0)

/*
 * Initialize an empty hash table.
 */
    static void
pyhash_init(ht)
    pyhashtab_T *ht;
{
    /* This zeroes all "pht_" entries and all the "hi_key" in "pht_smallarray". 
     * */
    vim_memset(ht, 0, sizeof(pyhashtab_T));
    ht->pht_array = ht->pht_smallarray;
    ht->pht_vals = ht->pht_smallvars;
    ht->pht_mask = HT_INIT_SIZE - 1;
}

    static void **
pyhash_lookup(ht, key)
    pyhashtab_T	*ht;
    void	*key;
{
    void	**hi;
    void	**freeitem = NULL;
    size_t	idx;
    size_t	perturb;
    size_t	index;
    PyObject	*obj;

    /*
     * Quickly handle the most common situations:
     * - return if there is no item at all
     * - skip over a removed item
     * - return if the item matches
     */
    idx = (((size_t) key) & ht->pht_mask);
    hi = &ht->pht_array[idx];

    if(*hi == NULL)
        return hi;
    if (PYOBJ_DELETED(ht->pht_vals[idx]))
        freeitem = hi;
    else if (*hi == key)
        return hi;

    /*
     * Need to search through the table to find the key.  The algorithm
     * to step through the table starts with large steps, gradually becoming
     * smaller down to (1/4 table size + 1).  This means it goes through all
     * table entries in the end.
     * When we run into a NULL key it's clear that the key isn't there.
     * Return the first available slot found (can be a slot of a removed
     * item).
     */
    for (perturb = (size_t) key; ; perturb >>= PERTURB_SHIFT)
    {
	idx = (int)((idx << 2) + idx + perturb + 1);
	index = idx & ht->pht_mask;
	hi = &ht->pht_array[index];
	obj = ht->pht_vals[index];
	if (*hi == NULL)
	    return freeitem == NULL ? hi : freeitem;
	if (*hi == key)
	{
	    if(PYOBJ_DELETED(obj))
	    {
		if(freeitem == NULL)
		    freeitem = hi;
	    }
	    else
		return hi;
	}
	else if(PYOBJ_DELETED(obj))
	    freeitem = hi;
    }
}

#define PHVAL(ht, hi) (ht.pht_vals[hi - ht.pht_array])
#define PYHASHITEM_EMPTY(ht, hi) ((*hi == NULL) || \
				  (PYOBJ_DELETED(PHVAL(ht, hi))))

/*
 * Add item "hi" with "key" to hashtable "ht".  "key" must not be NULL and
 * "hi" must have been obtained with pyhash_lookup() and point to an empty item.
 * "hi" is invalid after this!
 * Returns OK or FAIL (out of memory).
 */
    static int
pyhash_add_item(ht, hi, key, val)
    pyhashtab_T	*ht;
    void	**hi;
    void	*key;
    PyObject	*val;
{
    int	adding = PYHASHITEM_EMPTY((*ht), hi);
    if(adding)
    {
	/* If resizing failed before and it fails again we can't add an item. */
	if (ht->pht_error && pyhash_may_resize(ht, 0) == FAIL)
	    return FAIL;

	++ht->pht_used;
	if(PHVAL((*ht), hi) == NULL)
	    ++ht->pht_filled;
    }
    *hi = key;
    PHVAL((*ht), hi) = val;

    if(adding)
	/* When the space gets low may resize the array. */
	return pyhash_may_resize(ht, 0);
    else
	return OK;
}

/*
 * Add item with key "key" to hashtable "ht".
 * Returns FAIL when out of memory or the key is already present.
 */
    static int
pyhash_add(ht, key, val)
    pyhashtab_T	*ht;
    void	*key;
    PyObject	*val;
{
    void	**hi;

    hi = pyhash_lookup(ht, key);
    if (!PYHASHITEM_EMPTY((*ht), hi))
    {
	EMSG2(_(e_intern2), "pyhash_add()");
	return FAIL;
    }
    return pyhash_add_item(ht, hi, key, val);
}

/*
 * Remove item "hi" from  hashtable "ht".  "hi" must have been obtained with
 * pyhash_lookup().
 * The caller must take care of freeing the item itself.
 */
    static void
pyhash_remove(ht, hi)
    pyhashtab_T	*ht;
    void	**hi;
{
    --ht->pht_used;
    *hi = NULL;
    PHVAL((*ht), hi) = NULL;
    --ht->pht_filled;
    pyhash_may_resize(ht, 0);
}

/*
 * Shrink a hashtable when there is too much empty space.
 * Grow a hashtable when there is not enough empty space.
 * Returns OK or FAIL (out of memory).
 */
    static int
pyhash_may_resize(ht, minitems)
    pyhashtab_T	*ht;
    size_t	minitems;		/* minimal number of items */
{
    void	*temparray[HT_INIT_SIZE];
    PyObject	*temppyarray[HT_INIT_SIZE];
    void	**oldarray, **newarray;
    void	**olditem, **newitem;
    PyObject	**newpyitem, **oldpyarray, **newpyarray;
    size_t	idx;
    size_t	newi;
    size_t	todo;
    size_t	oldsize, newsize;
    size_t	minsize;
    size_t	newmask;
    size_t	perturb;
    size_t	i;

    if (minitems == 0)
    {
	/* Return quickly for small tables with at least two NULL items.  NULL
	 * items are required for the lookup to decide a key isn't there. */
	if (ht->pht_filled < HT_INIT_SIZE - 1
					 && ht->pht_array == ht->pht_smallarray)
	    return OK;

	/*
	 * Grow or refill the array when it's more than 2/3 full (including
	 * removed items, so that they get cleaned up).
	 * Shrink the array when it's less than 1/5 full.  When growing it is
	 * at least 1/4 full (avoids repeated grow-shrink operations)
	 */
	oldsize = ht->pht_mask + 1;
	if (ht->pht_filled * 3 < oldsize * 2 && ht->pht_used > oldsize / 5)
	    return OK;

	if (ht->pht_used > 1000)
	    minsize = ht->pht_used * 2;  /* it's big, don't make too much room */
	else
	    minsize = ht->pht_used * 4;  /* make plenty of room */
    }
    else
    {
	/* Use specified size. */
	if (minitems < ht->pht_used)	/* just in case... */
	    minitems = ht->pht_used;
	minsize = minitems * 3 / 2;	/* array is up to 2/3 full */
    }

    newsize = HT_INIT_SIZE;
    while (newsize < minsize)
    {
	newsize <<= 1;		/* make sure it's always a power of 2 */
	if (newsize <= 0)
	    return FAIL;	/* overflow */
    }

    if (newsize == HT_INIT_SIZE)
    {
	/* Use the small array inside the hashdict structure. */
	newarray = ht->pht_smallarray;
	newpyarray = ht->pht_smallvars;
	if (ht->pht_array == newarray)
	{
	    /* Moving from pht_smallarray to pht_smallarray!  Happens when there
	     * are many removed items.  Copy the items to be able to clean up
	     * removed items. */
	    mch_memmove(temparray, newarray, sizeof(temparray));
	    oldarray = temparray;
	    mch_memmove(temppyarray, newpyarray, sizeof(temppyarray));
	    oldpyarray = temppyarray;
	}
	else
	{
	    oldarray = ht->pht_array;
	    oldpyarray = ht->pht_vals;
	}
    }
    else
    {
	/* Allocate an array. */
	newarray = (void **)alloc(sizeof(void *) * newsize);
	newpyarray = (PyObject **)alloc(sizeof(PyObject *) * newsize);
	if (newarray == NULL)
	{
	    /* Out of memory.  When there are NULL items still return OK.
	     * Otherwise set pht_error, because lookup may result in a hang if
	     * we add another item. */
	    if (ht->pht_filled < ht->pht_mask)
		return OK;
	    ht->pht_error = TRUE;
	    return FAIL;
	}
	oldarray = ht->pht_array;
	oldpyarray = ht->pht_vals;
    }
    vim_memset(newarray, 0, sizeof(void *) * newsize);
    vim_memset(newpyarray, 0, sizeof(PyObject *) * newsize);

    /*
     * Move all the items from the old array to the new one, placing them in
     * the right spot.  The new array won't have any removed items, thus this
     * is also a cleanup action.
     */
    newmask = newsize - 1;
    todo = ht->pht_used;
    olditem = oldarray;
    for (i = 0 ; todo > 0 && i <= ht->pht_mask ; ++i)
    {
	if (!PYHASHITEM_EMPTY((*ht), olditem))
	{
	    /*
	     * The algorithm to find the spot to add the item is identical to
	     * the algorithm to find an item in pyhash_lookup().  But we only
	     * need to search for a NULL key, thus it's simpler.
	     */
	    newi = ((size_t) (*olditem) & newmask);
	    newitem = &newarray[newi];
	    idx = newi;

	    if (*newitem != NULL)
		for (perturb = (size_t) olditem; ; perturb >>= PERTURB_SHIFT)
		{
		    newi = ((newi << 2) + newi + perturb + 1);
		    idx = (newi & newmask);
		    newitem = &newarray[idx];
		    if (*newitem == NULL)
			break;
		}
	    *newitem = *olditem;
	    newpyarray[idx] = oldpyarray[i];
	    --todo;
	}
	++olditem;
    }

    if (ht->pht_array != ht->pht_smallarray)
	vim_free(ht->pht_array);
    ht->pht_array = newarray;
    ht->pht_vals = newpyarray;
    ht->pht_mask = newmask;
    ht->pht_filled = ht->pht_used;
    ht->pht_error = FALSE;

    return OK;
}

static pyhashtab_T dictrefs;
static pyhashtab_T listrefs;

    static void
set_ref_in_py(int copyID)
{
    size_t	i;
    dict_T	*dd;
    list_T	*ll;

    if(!PYINITIALISED)
	return;

    for(i = 0; i <= dictrefs.pht_mask ; i++)
    {
	dd = (dict_T *) dictrefs.pht_array[i];
	if(dd != NULL && !PYOBJ_DELETED(dictrefs.pht_vals[i]))
	{
	    dd->dv_copyID = copyID;
	    set_ref_in_ht(&dd->dv_hashtab, copyID);
	}
    }


    for(i = 0; i <= listrefs.pht_mask ; i++)
    {
	ll = (list_T *) listrefs.pht_array[i];
	if(ll != NULL && !PYOBJ_DELETED(listrefs.pht_vals[i]))
	{
	    ll->lv_copyID = copyID;
	    set_ref_in_list(ll, copyID);
	}
    }
}

    static int
set_string_copy(char_u *str, typval_T *tv)
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
