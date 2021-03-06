lattice
=======

Lattice is one of the basic structures employed in cicada which is
primarily used as an input, i.e. sentence.

JLF Format
----------

The native lattice format is JLF (JSON Lattice Format) which is
represented by the `JSON data format <http://www.json.org>`_.
Strings must be escaped like a C string, (see JSON specification), and
integers and floating points are discriminated by whether a fractional
point "." is included, or not.

.. code::

  lattice    ::= [arcs *(, arcs)]
  arcs       ::= [arc *(, arc)]
  arc        ::= [label, features(, attributes), distance]
  label      ::= JSON-STRING
  features   ::= {} | { JSON-STRING : JSON-FLOAT *(, JSON-STRING : JSON-FLOAT) }
  attributes ::= { JSON-STRING : value *(, JSON-STRING : value) }
  distance   ::= JSON-INT
  value      ::= JSON-STRING | JSON-FLOAT | JSON-INT

Each arc consists of label, features and distance field with optional
attributes. We allow arcs with ``<epsilon>`` label which allows us to
directly handle arbitrary confusion network.
In JLF, we can easily add extra features encoded in the lattice, such as

.. code:: json

  {"lattice-cost": 0.5, "acoustic": -5000.9}

Remark, the costs are interpreted as logarithmic value so that we can
compute score by "weight \cdot feature-function".
The optional attributes is a list of key-value pair of JSON string and
attribute value.
The attribute value can take either 64bit integer, floating point
value (double precision) or JSON string.

PLF Format
----------

cicada can read PLF (Python Lattice Format) used in `Moses <http://statmt.org/moses/>`_,
but does not support writing.

.. code::

  lattice  ::= (+(arcs,))
  arcs     ::= (+(arc,))
  arc      ::= (label, cost, distance)
  label    ::= PYTHON-STRING
  cost     ::= PYTHON-FLOAT
  distance ::= PYTHON-INT

The cost in the PLF is interpreted as a "lattice-cost" feature.

Examples:
---------

A lattice in JLF format:

.. code:: json

  [[["ein'\"en", {"lattice-cost": 1.0}, 1]],
   [["wettbewerbsbedingten", {"lattice-cost": 0.5}, 2],
    ["wettbewerbs", {"lattice-cost": 0.25}, 1],
    ["wettbewerb", {"lattice-cost": 0.25}, 1]],
   [["bedingten", {"lattice-cost": 1.0}, 1]],
   [["preissturz", {"lattice-cost": 0.5}, 2],
    ["preis", {"lattice-cost": 0.5}, 1]],
   [["sturz", {"lattice-cost": 1.0}, 1]]]

is equivalently represented by PLF as follows:

.. code:: python

  ((('ein\\"en',1.0,1),),
   (('wettbewerbsbedingten',0.5,2),('wettbewerbs',0.25,1),('wettbewerb',0.25, 1),),
   (('bedingten',1.0,1),),
   (('preissturz',0.5,2), ('preis',0.5,1),),
   (('sturz',1.0,1),),)

Visualization
-------------

You can convert a lattice as a `dot` file and use graphviz to
visualize it.

.. code:: bash

  cat lattice.txt | cicada --input-lattice --operation output:graphviz=true,lattice=true

Currently, the lattice should be in one-line, and should not span into multiple lines.

Tools
-----

`cicada_unite_lattice`

  Merge multiple lattices (or sentences) in one. Here, we simply compute the union sharing start/goal states.

`cicada_unite_sentence`

  Merge multiple sentences into one confusion-network via TER alignment.
  Support incremental merging by confusion-network-TER. (`--merge` flag)
  TER-computation by lower-cased word (`--lower` flag)
