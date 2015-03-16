## Interpreting the Data: Parallel Analysis with Sawzall ##

Rob Pike, Sean Dorward, Robert Griesemer, Sean Quinlan

## Abstract ##

Very large data sets often have a flat but regular
structure and span multiple disks and machines.
Examples include telephone call records, network logs,
and web document repositories. These large data sets
are not amenable to study using traditional database
techniques, if only because they can be too large to
fit in a single relational database.   On the other hand, many
of the analyses done on them can be expressed using
simple, easily distributed computations: filtering,
aggregation, extraction of statistics, and so on.

We present a system for automating such analyses.
A filtering phase, in which a query is expressed using
a new programming language, emits data to an
aggregation phase.  Both phases are distributed
over hundreds
or even thousands of computers.  The results are then
collated and saved to a file.  The design -- including
the separation into two phases, the form of the
programming language, and the properties of the
aggregators -- exploits the parallelism inherent
in having data and computation distributed across
many machines.

Published in:<br>
<i>Scientific Programming Journal</i><br>
Special Issue on Grids and Worldwide Computing<br>
Programming Models and Infrastructure <b>13</b>:<i>4</i>, pp. 227-298.<br>
<br>
Download: <a href='http://research.google.com/archive/sawzall.html'>PDF Version</a>

URL (Final): <a href='http://iospress.metapress.com/openurl.asp?genre=article&issn=1058-9244&volume=13&issue=4&spage=277"'>Journal link</a>

Animation: The paper references<br>
<a href='http://research.google.com/archive/sawzall-20030814.gif'>this movie</a>
showing how the distribution of requests to <a href='http://www.google.com'>google.com</a>
around the world changed through the day<br>
on <a href='http://www.google.com/search?q=August+14,+2003'>August 14, 2003</a>.