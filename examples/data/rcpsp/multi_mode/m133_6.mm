************************************************************************
file with basedata            : cm133_.bas
initial value random generator: 196717881
************************************************************************
projects                      :  1
jobs (incl. supersource/sink ):  18
horizon                       :  101
RESOURCES
  - renewable                 :  2   R
  - nonrenewable              :  2   N
  - doubly constrained        :  0   D
************************************************************************
PROJECT INFORMATION:
pronr.  #jobs rel.date duedate tardcost  MPM-Time
    1     16      0       49       13       49
************************************************************************
PRECEDENCE RELATIONS:
jobnr.    #modes  #successors   successors
   1        1          3           2   3   4
   2        1          3           5  10  12
   3        1          2           5   6
   4        1          3           7   8  11
   5        1          2           7   9
   6        1          3           7   8   9
   7        1          1          16
   8        1          3          10  12  15
   9        1          3          11  13  16
  10        1          1          13
  11        1          2          15  17
  12        1          2          13  16
  13        1          1          14
  14        1          1          17
  15        1          1          18
  16        1          1          18
  17        1          1          18
  18        1          0        
************************************************************************
REQUESTS/DURATIONS:
jobnr. mode duration  R 1  R 2  N 1  N 2
------------------------------------------------------------------------
  1      1     0       0    0    0    0
  2      1     6       1    0    4    9
  3      1     9       4    0    3    9
  4      1     1       0    3    8    3
  5      1     4       4    0    6    5
  6      1     6       0    3    6    1
  7      1     9       0    7    8    5
  8      1     4       9    0    2    2
  9      1     8       5    0    4    4
 10      1     7       0    9    6    4
 11      1     6       0    4    6    7
 12      1     7       0    1    7    3
 13      1    10       3    0    3    3
 14      1     3       0    2    5    2
 15      1     6       0    9    6    4
 16      1     5       8    0   10    6
 17      1    10       2    0    2    3
 18      1     0       0    0    0    0
************************************************************************
RESOURCEAVAILABILITIES:
  R 1  R 2  N 1  N 2
   10   12   86   70
************************************************************************
