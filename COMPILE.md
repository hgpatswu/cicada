Requirements

expgram: ngram language model training/indexing

Others:
Boost library     (http://www.boost.org/)
MPI (Open MPI)    (http://www.open-mpi.org/)
    REMARK: Under Linux, it is recommended not to use memory managers in open-mpi, which may conflict with your
    	    favorite memory managers, such as jemalloc/tcmalloc. To disable this, for instance, you
	    can edit "openmpi-mca-params.conf" and add(or disable) mca by, "memory = ^ptmalloc2"
	    For details, consult open-mpi documentation.

ICU               (http://site.icu-project.org/)

Optional:
	msgpack: http://msgpack.org
		 NOTE: msgpack-0.5.7 has a bug in which deletion may be called twice!
		       Grab the latest copy from https://github.com/msgpack/msgpack-c

	srilm:   Language model training (http://www-speech.sri.com/projects/srilm/)
	         For indexing, you still need "expgram"

	GIZA++:  alignment model training (http://code.google.com/p/giza-pp/)
	         or, you can uses moses (http://www.statmt.org/moses/) for alignment training with GIZA++
	         or, try berkeley aligner (http://code.google.com/p/berkeleyaligner/)
		 or, try postcat (http://www.seas.upenn.edu/~strctlrn/CAT/CAT.html).
		 
		 Remark that cicada can "align words" using symmetized training of berkeley aligner and/or posterior
		 constrained training of postcat with parameter smoothing via naive-Bayes or L0-norm.
	
	For better memory management:

	gperftools (http://code.google.com/p/gperftools/)
	jemalloc  (http://www.canonware.com/jemalloc/)

	   For Linux, you should install one of them for better memory performance
	   and to measure how many bytes malloced, since mallinfo is "broken" for memory more than 4GB.

Included libraries + licenses:

==========================================================
Murmurhash2:

All code is released to the public domain. For business purposes, Murmurhash is under the MIT license. 

==========================================================
Murmurhash3:

MurmurHash3 was written by Austin Appleby, and is placed in the public
domain. The author hereby disclaims copyright to this source code.

MIT license

==========================================================
libstemmer_c: Snowball stemmer (http://snowball.tartarus.org/)

All the software given out on this Snowball site is covered by the BSD License (see http://www.opensource.org/licenses/bsd-license.html ), with Copyright (c) 2001, Dr Martin Porter, and (for the Java developments) Copyright (c) 2002, Richard Boulton.

Essentially, all this means is that you can do what you like with the code, except claim another Copyright for it, or claim that it is issued under a different license. The software is also issued without warranties, which means that if anyone suffers through its use, they cannot come back and sue you. You also have to alert anyone to whom you give the Snowball software to the fact that it is covered by the BSD license.

We have not bothered to insert the licensing arrangement into the text of the Snowball software. 

==========================================================
WordNet-3.0: WordNet (http://wordnet.princeton.edu/)

### Actually, we do not include data, but uses c-library...

WordNet Release 3.0

This software and database is being provided to you, the LICENSEE, by  
Princeton University under the following license.  By obtaining, using  
and/or copying this software and database, you agree that you have  
read, understood, and will comply with these terms and conditions.:  
  
Permission to use, copy, modify and distribute this software and  
database and its documentation for any purpose and without fee or  
royalty is hereby granted, provided that you agree to comply with  
the following copyright notice and statements, including the disclaimer,  
and that the same appear on ALL copies of the software, database and  
documentation, including modifications that you make for internal  
use or for distribution.  
  
WordNet 3.0 Copyright 2006 by Princeton University.  All rights reserved.  
  
THIS SOFTWARE AND DATABASE IS PROVIDED "AS IS" AND PRINCETON  
UNIVERSITY MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR  
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, PRINCETON  
UNIVERSITY MAKES NO REPRESENTATIONS OR WARRANTIES OF MERCHANT-  
ABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR THAT THE USE  
OF THE LICENSED SOFTWARE, DATABASE OR DOCUMENTATION WILL NOT  
INFRINGE ANY THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR  
OTHER RIGHTS.  
  
The name of Princeton University or Princeton may not be used in  
advertising or publicity pertaining to distribution of the software  
and/or database.  Title to copyright in this software, database and  
any associated documentation shall at all times remain with  
Princeton University and LICENSEE agrees to preserve same.  

==========================================================
libLBFGS (http://www.chokkan.org/software/liblbfgs/)

The MIT License

Copyright (c) 1990 Jorge Nocedal
Copyright (c) 2007-2010 Naoaki Okazaki

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

==========================================================
boost.m4

The code of boost.m4 is released under GPLv3+ with the following additional
clause:

 Additional permission under section 7 of the GNU General Public
 License, version 3 ("GPLv3"):

 If you convey this file as part of a work that contains a
 configuration script generated by Autoconf, you may do so under
 terms of your choice.

This clause has been written by FSF lawyers for Autotools.  If you have any
concerns about legal issues, do not contact me as I Am Not A Lawyer.  I
*think* you can get advices at <copyright-clerk at fsf dot org>.  The intent
here is to keep the code Free but to allow anyone to *use* it.


==========================================================
liblinear

Copyright (c) 2007-2011 The LIBLINEAR Project.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither name of copyright holders nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

==========================================================
edmonds_optimum_branching.hpp

MIT License

Copyright (c) 2007 Ali Tofigh, Erik Sjölund

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

==========================================================
lz4

   LZ4 - Fast LZ compression algorithm
   Header File
   Copyright (C) 2011-2012, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
   - LZ4 source repository : http://code.google.com/p/lz4/

==========================================================
quicklz

Fast data compression library
Copyright (C) 2006-2011 Lasse Mikkel Reinhold
lar@quicklz.com

QuickLZ can be used for free under the GPL 1, 2 or 3 license (where anything 
released into public must be open source) or under a commercial license if such 
has been acquired (see http://www.quicklz.com/order.html). The commercial license 
does not cover derived or ported versions created by third parties under GPL.

==========================================================
fastlz

  FastLZ is distributed using the MIT license, see file LICENSE
  for details.

  FastLZ - lightning-fast lossless compression library

  Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
==========================================================
Eigen
Since the 3.1.1 release, Eigen is licensed under the MPL2. We refer to the MPL2 FAQ for initial questions.

==========================================================
xxhash

   xxHash - Fast Hash algorithm
   Header File
   Copyright (C) 2012, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
  
       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.
  
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

        You can contact the author at :
        - xxHash source repository : http://code.google.com/p/xxhash/

==========================================================
cityhash

Copyright (c) 2011 Google, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

CityHash, by Geoff Pike and Jyrki Alakuijala

We provide reference implementations in C++, with a friendly MIT license.

==========================================================
CG_DESCENT

       ________________________________________________________________
      |      A conjugate gradient method with guaranteed descent       |
      |             C-code Version 1.1  (October 6, 2005)              |
      |                    Version 1.2  (November 14, 2005)            |
      |                    Version 2.0  (September 23, 2007)           |
      |                    Version 3.0  (May 18, 2008)                 |
      |                    Version 4.0  (March 28, 2011)               |
      |                    Version 4.1  (April 8, 2011)                |
      |                    Version 4.2  (April 14, 2011)               |
      |                    Version 5.0  (May 1, 2011)                  |
      |                    Version 5.1  (January 31, 2012)             |
      |                    Version 5.2  (April 17, 2012)               |
      |                    Version 5.3  (May 18, 2012)                 |
      |                    Version 6.0  (November 6, 2012)             |
      |                    Version 6.1  (January 27, 2013)             |
      |                    Version 6.2  (February 2, 2013)             |
      |                                                                |
      |           William W. Hager    and   Hongchao Zhang             |
      |          hager@math.ufl.edu       hozhang@math.lsu.edu         |
      |                   Department of Mathematics                    |
      |                     University of Florida                      |
      |                 Gainesville, Florida 32611 USA                 |
      |                      352-392-0281 x 244                        |
      |                                                                |
      |                 Copyright by William W. Hager                  |
      |                                                                |
      |          http://www.math.ufl.edu/~hager/papers/CG              |
      |                                                                |
      |  Disclaimer: The views expressed are those of the authors and  |
      |              do not reflect the official policy or position of |
      |              the Department of Defense or the U.S. Government. |
      |                                                                |
      |      Approved for Public Release, Distribution Unlimited       |
      |________________________________________________________________|
       ________________________________________________________________
      |This program is free software; you can redistribute it and/or   |
      |modify it under the terms of the GNU General Public License as  |
      |published by the Free Software Foundation; either version 2 of  |
      |the License, or (at your option) any later version.             |
      |This program is distributed in the hope that it will be useful, |
      |but WITHOUT ANY WARRANTY; without even the implied warranty of  |
      |MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   |
      |GNU General Public License for more details.                    |
      |                                                                |
      |You should have received a copy of the GNU General Public       |
      |License along with this program; if not, write to the Free      |
      |Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, |
      |MA  02110-1301  USA                                             |
      |________________________________________________________________|