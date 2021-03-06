************************************************************************
file with basedata            : cn360_.bas
initial value random generator: 1606757726
************************************************************************
projects                      :  1
jobs (incl. supersource/sink ):  18
horizon                       :  120
RESOURCES
  - renewable                 :  2   R
  - nonrenewable              :  3   N
  - doubly constrained        :  0   D
************************************************************************
PROJECT INFORMATION:
pronr.  #jobs rel.date duedate tardcost  MPM-Time
    1     16      0       30        3       30
************************************************************************
PRECEDENCE RELATIONS:
jobnr.    #modes  #successors   successors
   1        1          3           2   3   4
   2        3          3           6  12  17
   3        3          1           5
   4        3          3           9  11  16
   5        3          3           6   7   8
   6        3          1          11
   7        3          3           9  10  12
   8        3          2          10  13
   9        3          2          15  17
  10        3          2          14  16
  11        3          2          13  14
  12        3          3          13  14  16
  13        3          1          15
  14        3          1          15
  15        3          1          18
  16        3          1          18
  17        3          1          18
  18        1          0        
************************************************************************
REQUESTS/DURATIONS:
jobnr. mode duration  R 1  R 2  N 1  N 2  N 3
------------------------------------------------------------------------
  1      1     0       0    0    0    0    0
  2      1     4       5    0    8    7   10
         2     8       0    5    7    4   10
         3    10       0    3    7    4   10
  3      1     7       0    8    3    7    5
         2     9       0    8    2    7    4
         3    10       5    0    2    7    2
  4      1     2       3    0    3    6    8
         2     2       0    5    4    4   10
         3     2       5    0    4    4    2
  5      1     6       7    0    4    3    5
         2     6       9    0    5    2    4
         3    10       7    0    1    2    4
  6      1     5       0    6    8    6    8
         2     5       0    5   10    9    5
         3     9       0    3    2    6    4
  7      1     1       0    2    2    9    9
         2     7       3    0    2    8    8
         3     9       0    1    2    8    5
  8      1     1       7    0    8    7    8
         2     2       5    0    7    6    8
         3     3       0    8    6    6    7
  9      1     4       4    0    8    7    8
         2     8       0    6    6    7    5
         3     8       3    0    5    7    6
 10      1     1       6    0    9    8   10
         2     6       0    7    7    7    6
         3     8       0    6    6    7    5
 11      1     3       9    0    8   10    7
         2     7       8    0    6    9    6
         3    10       0    8    6    8    4
 12      1     1       7    0    3    8    9
         2     1       4    0    4    9   10
         3     8       3    0    2    6    5
 13      1     4       0   10    9    7    6
         2     4       7    0    8    9    6
         3     6       0    9    6    6    6
 14      1     8      10    0   10    6    5
         2     8       0    4   10    6    5
         3    10       9    0    5    3    4
 15      1     1       5    0    3    9    6
         2     3       4    0    3    8    6
         3     5       0    4    3    7    5
 16      1     1       0    8   10    8    8
         2     1       0    8   10    9    7
         3     3       0    5    8    6    5
 17      1     1       0   10    9    8    4
         2     4       1    0    6    6    3
         3     9       0   10    3    4    1
 18      1     0       0    0    0    0    0
************************************************************************
RESOURCEAVAILABILITIES:
  R 1  R 2  N 1  N 2  N 3
   26   26  110  123  119
************************************************************************
