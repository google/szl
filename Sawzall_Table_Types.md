## Sawzall Table Types ##

In the Sawzall programming language you output data by emitting it to a `table`.  There are a few kinds of table; each type aggregates input in a different way.

Declaration syntax (EBNF):
```
OutputDecl = table_name: OutputType ';'.
OutputType = 'table' table_kind [Param] [Indices] 'of' Element [Weight] [File|Proc] [Format].
table_name = identifier.
table_kind = identifier.
Param = '(' N ')'.                             
Indices = { '[' Component ']' }.
Element = Component.
Weight = 'weight' Component.
Component = [identifier ':'] Type.             
File = 'file' '(' ArgumentList ')'.
Proc = 'proc' '(' ArgumentList ')'.            
Format = 'format' '(' ArgumentList ')'.        
ArgumentList = StringLiteral {',' Expression}.
```

With the semantic restrictions:
  * In Param, N can only be const uint
  * In Component, Type cannot be 'function' or 'table'.
  * In File and Proc, ArgumentList can reference identifiable Indices, Element must be bytes or used with Format.
  * In Format, ArgumentList can reference identifiable Element.

| **Type** | **Description** | **Param**<br> <code>(N)</code> <table><thead><th> <b>Indices</b><br> <code>i</code><sub>1</sub><code>: T</code><sub>1</sub> ... <code>i</code><sub>n</sub><code>: T</code><sub>n</sub> </th><th> <b>Element</b><br> <code>of T</code> </th><th> <b>Weight</b><br> <code>weight T</code> </th><th> <b>Format</b><br><code>format(...)</code></th><th> <b>File/Proc</b><br> <code>file(...)</code> <br> <code>proc(...)</code> </th></thead><tbody>
<tr><td> collection </td><td> A simple collection or concatenation of the data. </td><td> no </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> no </td><td> yes </td><td> yes </td></tr>
<tr><td> maximum </td><td> A precise sample of the <var>N</var> highest-weighted data items. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = basic or tuple, proto </td><td> yes </td><td> no </td></tr>
<tr><td> minimum </td><td> A precise sample of the <var>N</var> lowest-weighted data items. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = basic or tuple, proto </td><td> yes </td><td> no </td></tr>
<tr><td> recordio </td><td> An unindexed collection written directly to a recordio file. </td><td> yes </td><td> n = 0 </td><td> T = basic </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> sample </td><td> A statistical sampling of <var>N</var> items. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> sum </td><td> An arithmetic sum of the data. </td><td> no </td><td> n <= 0, T = basic, composite </td><td> T = int, float, time or map, tuple, proto thereof </td><td> no </td><td> no </td><td> no </td></tr>
<tr><td> top </td><td> Statistical samplings that record the 'top <var>N</var>' data items. </td><td> no </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = int, float or tuple, proto thereof </td><td> yes </td><td> no </td></tr>
<tr><td> unique </td><td> Statistical estimators for the total number of unique data items. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> set </td><td> A set of size at most <var>N</var>. Larger sets are discarded. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> quantile </td><td> Approximate <a href='http://mathworld.wolfram.com/Quantile.html'>N-tiles</a> for data items from an ordered domain. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, tuple, proto </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> distinctsample </td><td> A uniform sample of a given size from a set of all values seen. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = int, float, time or tuple, proto thereof </td><td> yes </td><td> no </td></tr>
<tr><td> inversehistogram </td><td> An approximate histogram of unique values. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = int, float, time or tuple, proto thereof </td><td> yes </td><td> no </td></tr>
<tr><td> weightedsample </td><td> A statistical sampling of <var>N</var> items biased by weights. </td><td> yes </td><td> n <= 0, T = basic, composite </td><td> T = basic, composite </td><td> T = int or float </td><td> yes </td><td> no </td></tr>
<tr><td> text </td><td> An unindexed collection written directly to a plain file. </td><td> no </td><td> n = 0 </td><td> T = string, bytes </td><td> no </td><td> yes </td><td> no </td></tr>
<tr><td> mrcounter </td><td> An integer counter available to the invoking program. </td><td> no </td><td> n = 0 </td><td> T = int </td><td> no </td><td> no </td><td> no </td></tr>