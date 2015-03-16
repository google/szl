### Overview ###

The concept of the "definedness" of an expression or variable in Sawzall is an important but often misunderstood part of the language.  This document attempts to provide some insight into the issue.

## Places where _undef_ Can and Cannot Occur ##

**Expressions** can have undefined values.

**Non-static variables** can have undefined values in these cases:
  * The declaration does not contain an initializer.
  * The initialization failed (informally, the initializer was undefined).
  * The right side of an assignment failed (informally, the right side was undefined) and the `--ignore_undefs` command line option is in effect.

**Static variables** cannot have undefined values.  An attempt to initialize a static variable with an undefined value causes the program to terminate, even with `--ignore_undefs`.

**Tuple fields, array elements, map keys and map values** cannot have undefined values; if such an entity would have been undefined, then the entire tuple, array or map is itself undefined.

**Composite literal elements** cannot have undefined values; if any element would have been undefined, then the entire expression containing the composite literal is undefined.

**Function parameters** cannot initially have undefined values, because whenever a function argument is undefined the function is not called.  (The `def()` intrinsic is a special case.)

### Expressions ###

Generally whenever any evaluated part of an expression is undefined (except in `def()`) the entire expression is undefined.

Any use of an undefined variable or function result terminates evaluation of the expression out to the nearest enclosing `def()`.  The next action depends on the immediate context in which the terminated part of the expression occurs; if it is:
  * The initializer for a static variable, the program terminates.
  * The initializer for a nonstatic variable, the variable is undefined.
  * A `return` statement, the function result is undefined.
  * A call to `def()`, the result of the call is `false`.
  * An expression used in a statement other than `return`, the program terminates unless `--ignore_undefs` is in effect, in which case execution of the statement terminates.  This includes the special case of an _expression statement_.  Note that other expressions in the statement may have been successfully evaluated before this expression.
  * An assignment statement - in addition to the above, under `--ignore_undefs`, the variable on the left side is made undefined.  When the left side is an array element, map element or tuple field, the variable holding the array, map or tuple is made undefined.

Even when the evaluation of an expression is terminated because of an attempt to use an undefined value, other parts of the expression may already have been evaluated.  In the following example, `f()` may be evaluated before evaluation is terminated:
```
   x = f() + y;  # when y is undefined
```
Parts of an expression that are not evaluated do not cause evaluation to terminate, even if undefined values occur.  In the following example, expression evaluation is completed:
```
   flag = (2 > 3) && y;  # when y is undefined
```
And of course a simple variable on the left side of an assignment is not evaluated, so:
```
   x: int;
   x = 1;     # OK even though x was undefined
   y: array of int;
   y[1] = 2;  # fails because the value of y was needed
```

## Operations ##

When an operation fails, e.g. division by zero, the operation resulted in an undefined value.

The effect is the same as if an undefined variable had been used; that is, there is actually no value at all because the failure terminates evaluation in the same manner in which use of an undefined variable terminates evaluation.

## The `def()` Intrinsic Function ##

The `def()` intrinsic naturally looks at its argument and returns a boolean value indicating whether the argument was undefined.

In practice the evaluation of the argument to `def()` either succeeds, in which case it returns `true`, or is prematurely terminated due to an error or the use of an undefined variable, in which case it returns `false`.

When input may have corrupt data, the `def()` intrinsic can be used to compensate or to ignore the record, so the entire program does not terminate.

## Function and Intrinsic Function Results ##

Intrinsic functions can return undefined values to indicate failure.  The [guide to intrinsic functions](http://szl.googlecode.com/svn/doc/sawzall-intrinsics.html) says which intrinsic functions can return undefined.

Ordinary functions can also return undefined values.  As with intrinsics, this is typically used deliberately as a way to indicate failure.

## `__undef_cnt` and `__undef_details` ##

Two predefined table variables track references to undefined values.  There is an implicit `emit _undef_cnt <- 1` whenever an undefined value causes the program to terminate (or would have caused termination except for `--ignore_undefs`).  At the same time there is an implicit `emit _undef_details[`_error message_`] <= 1`.

## Error Messages ##

The Szl interpreter attempts to propagate error messages so that when an undefined value is used the error message shows the original problem.  For example:
```
   x := 1/0;
   y := x+1;
   z: int;
   z = y;
```
yields an error message like:

`undefined value at test.szl:1: z = y (probably because "y" was undefined due to an error at test.szl:1: 1/0 (divide by zero error: 1 / 0))`

Propagation of error messages is not perfect, so it is possible to get a slightly misleading message.

## Warnings ##

Since the compiler does limited constant folding and value propagation, some expressions will yield warnings about expressions known to be undefined or unnecessary uses of the `def()` intrinsic.  For example:
```
   x := 1/0;     # warning about divide by zero
   y := x;       # warning that "y" is undefined
   if (!def(y))  # warning about unnecessary def() because y is undefined
     y = 0;
   z := 1 / nrand(2);   # no way to know if z will be defined
   if (def(z)) {        # no warning, but now we know z is defined
     w := z;
     if (def(w))
       w = 0;           # warning about unnecessary def() because w is defined
   }
```
Many programs with defensive use of `def()` to check for errors will produce warnings.  It takes some practice to recognize when `def()` is needed and when it is not needed.