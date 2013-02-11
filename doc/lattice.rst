=======
lattice
=======

-------------------------------------
A description for JSON lattice format
-------------------------------------

:Author: Taro Watanabe <taro.watanabe@nict.go.jp>
:Date:   2013-2-11

JLF Format
----------

The native lattice format is JLF (JSON Lattice Format) which is
represented by the `JSON data format <http://www.json.org>`_.
Strings must be escaped (see JSON specification).




PLF Format
----------

cicada can read PLF (Python Lattice Format) used in `Moses <http://statmt.org/moses/>`_, but do not
support writing.




Examples:
---------

PLF:

::

  ((('ein\\"en',1.0,1),),(('wettbewerbsbedingten',0.5,2),('wettbewerbs',0.25,1), ('wettbewerb',0.25, 1),),(('bedingten',1.0,1),),(('preissturz',0.5,2), ('preis',0.5,1),),(('sturz',1.0,1),),)


JLF:

::

  [[["ein'\"en", {"lattice-cost": 1.0}, 1]], [["wettbewerbsbedingten", {"lattice-cost": 0.5}, 2], ["wettbewerbs", {"lattice-cost": 0.25}, 1], ["wettbewerb", {"lattice-cost": 0.25}, 1]], [["bedingten", {"lattice-cost": 1.0}, 1]], [["preissturz", {"lattice-cost": 0.5}, 2], ["preis", {"lattice-cost": 0.5}, 1]], [["sturz", {"lattice-cost": 1.0}, 1]]]


For detail on PLF, see Moses web pages. In JLF, we can easily add extra features encoded in the lattice,
by inserting new one in {}, such as

  {"lattice-cost": 0.5, "accoustic": -5000.9}

Remark, the costs are interpreted as logarithmic value so that we can compute score by "weight \cdot feature-function".


We allow <epsilon> node which allows us to directly handle confusion nework.
Inside cicada, it is recommended to perform remove-epsilon, before intersecting with grammar 

Tools
-----

cicada_unite_lattice

	Merge multiple lattices (or sentences) in one. Here, we simply compute the union sharing start/goal states.
	Optionally dump output in graphviz format (--graphviz flag).

cicada_unite_sentence

	Merge multiple sentences into one confusion-network via TER alignment.
	Support incrementat merging by confusion-network-TER. (--merge flag)
	TER-conputation by lower-cased word (--lower flag)
	Dump in graphviz format (--graphviz flag)

