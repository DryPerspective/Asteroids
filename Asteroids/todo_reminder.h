#ifndef TODO_MACRO
#define TODO_MACRO


/*
*  A simple tool to keep track of what is left to do as development goes.
*  When we reach a comfortable position, this header and all its calls will be removed from the program, since
*  this is obviously nonstandard.
*  Until then, it's good to have a list.
*/

#include <cassert>

#ifdef _DEBUG
#define PRAGMA(p)		_Pragma(#p)
#define TODO(something)		PRAGMA(message("To do: " something));
#else
#define TODO(args) static_assert(false, "Todos left undone");
#endif



#endif