#!/usr/bin/env python
#
#  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
#
### a wrapper script (similar to phrase-extract in moses)
### we support only "extraction" meaning only step 5 and 6
### TODO: use argparse for command-lines...?

import threading
import multiprocessing

import time
import sys
import os, os.path
import string
import re
import subprocess

from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
	
    # output directory/filename prefix
    make_option("--root-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="root directory for outputs"),
    make_option("--corpus-dir", default="", action="store", type="string",
                metavar="PREFIX", help="corpus directory (default: ${root_dir}/corpus)"),
    make_option("--giza-f2e", default="", action="store", type="string",
                metavar="DIRECTORY", help="giza directory for P(f|e) (default: ${root_dir}/giza.${f}-${e})"),
    make_option("--giza-e2f", default="", action="store", type="string",
                metavar="DIRECTORY", help="giza directory for P(e|f) (default: ${root_dir}/giza.${e}-${f})"),
    make_option("--model-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="model directory (default: ${root_dir}/model)"),
    make_option("--alignment-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="alignment directory (default: ${model_dir})"),
    make_option("--lexical-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="lexical transltion table directory (default: ${model_dir)"),
    
    ### source/target flags
    make_option("--f", default="F", action="store", type="string",
                metavar="SUFFIX", help="source (or 'French')  language suffix for training corpus"),
    make_option("--e", default="E", action="store", type="string",
                metavar="SUFFIX", help="target (or 'English') language suffix for training corpus"),
    ### span...
    make_option("--sf", default="SF", action="store", type="string",
                metavar="SUFFIX", help="source (or 'French')  span suffix for training corpus"),
    make_option("--se", default="SE", action="store", type="string",
                metavar="SUFFIX", help="target (or 'English') span suffix for training corpus"),
    ### forest!
    make_option("--ff", default="SF", action="store", type="string",
                metavar="SUFFIX", help="source (or 'French')  forest suffix for training corpus"),
    make_option("--fe", default="SE", action="store", type="string",
                metavar="SUFFIX", help="target (or 'English') forest suffix for training corpus"),
    
    # data prefix
    make_option("--corpus", default="corpus", action="store", type="string",
                help="bilingual trainging corpus"),

    # alignment method
    make_option("--alignment", default="grow-diag-final-and", action="store", type="string",
                help="alignment methods (default: grow-diag-final-and)"),
    
    
    # steps
    make_option("--first-step", default=4, action="store", type="int", metavar='STEP', help="first step (default: 4)"),
    make_option("--last-step",  default=6, action="store", type="int", metavar='STEP', help="last step  (default: 6)"),


    ## option for lexicon
    make_option("--lexicon-inverse", default=None, action="store_true", help="use inverse alignment"),
    make_option("--lexicon-prior", default=0.1, action="store", type="float", metavar="PRIOR", help="lexicon model prior (default: 0.1)"),
    
    # option for extraction
    make_option("--phrase", default=None, action="store_true", help="extract phrase"),
    make_option("--scfg",   default=None, action="store_true", help="extract SCFG"),
    make_option("--ghkm",   default=None, action="store_true", help="extract GHKM (tree-to-string)"),
    make_option("--tree",   default=None, action="store_true", help="extract tree-to-tree"),

    make_option("--non-terminal", default="[x]", action="store", type="string", help="default non-terminal for GHKM rule (default: [x])"),

    make_option("--max-sentence-length", default=0, action="store", type="int",
                metavar="LENGTH", help="maximum sentence size (default: 0 == no limit)"),
    
    make_option("--max-span-source", default=15, action="store", type="int",
                metavar="LENGTH", help="maximum source span size (default: 15)"),
    make_option("--max-span-target", default=20, action="store", type="int",
                metavar="LENGTH", help="maximum target span size (default: 20)"),
    make_option("--min-hole-source", default=1, action="store", type="int",
                metavar="LENGTH", help="minimum source hole size (default: 1)"),
    make_option("--min-hole-target", default=1, action="store", type="int",
                metavar="LENGTH", help="minimum target hole size (default: 1)"),
    make_option("--max-length", default=7, action="store", type="int",
                metavar="LENGTH", help="maximum terminal length (default: 7)"),
    make_option("--max-fertility", default=4, action="store", type="int",
                metavar="FERTILITY", help="maximum terminal fertility (default: 4)"),
    make_option("--max-nodes", default=15, action="store", type="int",
                metavar="NODES", help="maximum rule nodes (default: 15)"),
    make_option("--max-height", default=4, action="store", type="int",
                metavar="HEIGHT", help="maximum rule height (default: 4)"),
    make_option("--max-compose", default=0, action="store", type="int",
                metavar="COMPOSE", help="maximum rule composition (default: 0)"),
    make_option("--max-scope", default=0, action="store", type="int",
                metavar="SCOPE", help="maximum rule scope (default: 0)"),
    make_option("--collapse-source", default=None, action="store_true",
                help="collapse source side for CKY parsing"),
    make_option("--collapse-target", default=None, action="store_true",
                help="collapse target side for CKY parsing"),
    make_option("--exhaustive", default=None, action="store_true",
                help="exhaustive extraction in SCFG, GHKM and Tree"),
    make_option("--constrained", default=None, action="store_true",
                help="constrained extraction in SCFG, GHKM and Tree"),
    make_option("--project", default=None, action="store_true",
                help="project non-terminal symbols in GHKM"),
    
    make_option("--ternary", default=None, action="store_true",
                help="extract ternary rule"),
    make_option("--sentential", default=None, action="store_true",
                help="extract sentential rule"),
    
    ## max-malloc
    make_option("--max-malloc", default=8, action="store", type="float",
                metavar="MALLOC", help="maximum memory in GB (default: 8)"),

    # CICADA Toolkit directory
    make_option("--cicada-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="cicada directory"),
    # MPI Implementation.. if different from standard location...
    make_option("--mpi-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="MPI directory"),

    # perform threading or MPI training    
    make_option("--mpi", default=0, action="store", type="int",
                help="# of processes for MPI-based parallel processing. Identical to --np for mpirun"),
    make_option("--mpi-host", default="", action="store", type="string",
                help="list of hosts to run job. Identical to --host for mpirun", metavar="HOSTS"),
    make_option("--mpi-host-file", default="", action="store", type="string",
                help="host list file to run job. Identical to --hostfile for mpirun", metavar="FILE"),
    
    make_option("--threads", default=2, action="store", type="int",
                help="# of thrads for thread-based parallel processing"),
    
    make_option("--pbs", default=None, action="store_true",
                help="PBS for launching processes"),
    make_option("--pbs-queue", default="ltg", action="store", type="string",
                help="PBS queue for launching processes (default: ltg)", metavar="NAME"),

    ## debug messages
    make_option("--debug", default=0, action="store", type="int"),
    ])


### dump to stderr
stdout = sys.stdout
sys.stdout = sys.stderr

def run_command(command):
    retcode = subprocess.Popen(command, shell=True).wait()
    if retcode < 0:
        raise ValueError, "cannnot run? " + command

def compressed_file(file):
    if not file:
        return file
    if os.path.exists(file):
        return file
    if os.path.exists(file+'.gz'):
	return file+'.gz'
    if os.path.exists(file+'.bz2'):
	return file+'.bz2'
    (base, ext) = os.path.splitext(file)
    if ext == '.gz' or ext == '.bz2':
	if os.path.exists(base):
	    return base
    return file

class PBS:
    def __init__(self, queue="", workingdir=os.getcwd()):
        self.queue = queue
        self.workingdir = workingdir
        self.tmpdir = None
        self.tmpdir_spec = None

        if os.environ.has_key('TMPDIR'):
            self.tmpdir = os.environ['TMPDIR']

        if os.environ.has_key('TMPDIR_SPEC'):
            self.tmpdir_spec = os.environ['TMPDIR_SPEC']
            
    def run(self, command="", threads=1, memory=0.0, name="name", mpi=None, logfile=None):
        popen = subprocess.Popen("qsub -S /bin/sh", shell=True, stdin=subprocess.PIPE)

        pipe = popen.stdin
        
        pipe.write("#!/bin/sh\n")
        pipe.write("#PBS -N %s\n" %(name))
        pipe.write("#PBS -W block=true\n")
        
        if logfile:
            pipe.write("#PBS -e %s\n" %(logfile))
        else:
            pipe.write("#PBS -e /dev/null\n")
        pipe.write("#PBS -o /dev/null\n")
        
        if self.queue:
            pipe.write("#PBS -q %s\n" %(self.queue))

        if mpi:
            if memory > 0.0:
                if memory < 1.0:
                    pipe.write("#PBS -l select=%d:ncpus=3:mpiprocs=1:mem=%dmb\n" %(mpi.number, int(memory * 1000)))
                else:
                    pipe.write("#PBS -l select=%d:ncpus=3:mpiprocs=1:mem=%dgb\n" %(mpi.number, int(memory)))
            else:
                pipe.write("#PBS -l select=%d:ncpus=3:mpiprocs=1\n" %(mpi.number))
                
        else:
            if memory > 0.0:
                if memory < 1.0:
                    pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1:mem=%dmb\n" %(threads, int(memory * 1000)))
                else:
                    pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1:mem=%dgb\n" %(threads, int(memory)))
            else:
                pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1\n" %(threads))
        
        # setup TMPDIR and TMPDIR_SPEC
        if self.tmpdir:
            pipe.write("export TMPDIR=%s\n" %(self.tmpdir))
        if self.tmpdir_spec:
            pipe.write("export TMPDIR_SPEC=%s\n" %(self.tmpdir_spec))
            
        pipe.write("cd \"%s\"\n" %(self.workingdir))

        if mpi:
            pipe.write("%s %s\n" %(mpi.mpirun, command))
        else:
            pipe.write("%s\n" %(command))
        
        pipe.close()
        popen.wait()
            
class MPI:
    
    def __init__(self, dir="", hosts="", hosts_file="", number=0):
        
	self.dir = dir
	self.hosts = hosts
        self.hosts_file = hosts_file
        self.number = number
	
        if self.dir:
            if not os.path.exists(self.dir):
                raise ValueError, self.dir + " does not exist"
            self.dir = os.path.realpath(self.dir)

        if self.hosts_file:
            if not os.path.exists(self.hosts_file):
                raise ValueError, self.hosts_file + " does no exist"
            self.hosts_file = os.path.realpath(hosts_file)

        self.bindir = self.dir
	
        for binprog in ['mpirun']:
            if self.bindir:
                prog = os.path.join(self.bindir, 'bin', binprog)
                if not os.path.exists(prog):
                    prog = os.path.join(self.bindir, binprog)
                    if not os.path.exists(prog):
                        raise ValueError, prog + " does not exist at " + self.bindir
                    
                setattr(self, binprog, prog)
            else:
                setattr(self, binprog, binprog)
                
    def run(self, command):
        mpirun = self.mpirun
        #if self.dir:
        #    mpirun += ' --prefix %s' %(self.dir)
        if self.number > 0:
            mpirun += ' --np %d' %(self.number)
        if self.hosts:
            mpirun += ' --host %s' %(self.hosts)
        elif self.hosts_file:
            mpirun += ' --hostfile %s' %(self.hosts_file)
	mpirun += ' ' + command

	run_command(mpirun)


class CICADA:
    def __init__(self, dir=""):

	self.dir = dir	
	if not dir: return
	
	if not os.path.exists(self.dir):
	    raise ValueError, self.dir + " does not exist"
	
	self.dir = os.path.realpath(self.dir)
        
	self.bindirs = []
	for dir in ('bin', 'progs', 'scripts'): 
	    bindir = os.path.join(self.dir, dir)
	    if os.path.exists(bindir) and os.path.isdir(bindir):
		self.bindirs.append(bindir)
        self.bindirs.append(self.dir)
	
        for binprog in ('cicada_alignment',
                        ## step 4
                        'cicada_lexicon', 
                        ## step 5
                        'cicada_extract_phrase', 'cicada_extract_phrase_mpi',
                        'cicada_extract_scfg',   'cicada_extract_scfg_mpi',
                        'cicada_extract_ghkm',   'cicada_extract_ghkm_mpi',
                        'cicada_extract_tree',   'cicada_extract_tree_mpi',
                        ## step6
                        'cicada_extract_score', 'cicada_extract_score_mpi',):
	    
	    for bindir in self.bindirs:
		prog = os.path.join(bindir, binprog)
		if os.path.exists(prog):
		    setattr(self, binprog, prog)
		    break
	    if not hasattr(self, binprog):
		raise ValueError, binprog + ' does not exist'
        
class Corpus:

    def __init__(self, corpus="", f="", e="", sf="", se="", ff="", fe=""):

        self.source_tag = f
        self.target_tag = e

        self.source_span_tag = sf
        self.target_span_tag = se

        self.source_forest_tag = ff
        self.target_forest_tag = fe
        
        self.source = compressed_file(corpus+'.'+f)
        self.target = compressed_file(corpus+'.'+e)
        
        self.source_span = compressed_file(corpus+'.'+sf)
        self.target_span = compressed_file(corpus+'.'+se)
        
        self.source_forest = compressed_file(corpus+'.'+ff)
        self.target_forest = compressed_file(corpus+'.'+fe)

class Alignment:
    def __init__(self, alignment_dir="", alignment=""):
        self.alignment = compressed_file(os.path.join(alignment_dir, 'aligned.'+alignment))

        if not os.path.exists(self.alignment):
            raise ValueError, "no alignment data %s" %(self.alignment)
        

class Lexicon:
    def __init__(self, cicada=None, corpus=None, alignment=None, lexical_dir="", prior=0.1,
                 inverse=None,
                 threads=4, mpi=None, pbs=None,
                 debug=None):
        self.threads = threads
        self.mpi = mpi
        self.pbs = pbs
        
        self.source_target = compressed_file(os.path.join(lexical_dir, 'lex.f2n'))
        self.target_source = compressed_file(os.path.join(lexical_dir, 'lex.n2f'))
        self.makedirs = lexical_dir
        self.data = []

        self.data.append(corpus.source)
        self.data.append(corpus.target)

        command = "%s" %(cicada.cicada_lexicon)
        
        command += " --source \"%s\"" %(corpus.source)
        command += " --target \"%s\"" %(corpus.target)
        command += " --alignment \"%s\"" %(alignment.alignment)
        
        command += " --output-source-target \"%s.gz\"" %(os.path.join(lexical_dir, 'lex.f2n'))
        command += " --output-target-source \"%s.gz\"" %(os.path.join(lexical_dir, 'lex.n2f'))
        
        command += " --variational-bayes"
        command += " --prior %g" %(prior)
        
        if inverse:
            command += " --inverse"

        command += " --threads %d" %(threads)

        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"
        
        self.command = command

    def run(self):
        for file in self.data:
            if not os.path.exists(file):
                raise ValueError, "no file: " + file

        if not os.path.exists(self.makedirs):
            os.makedirs(self.makedirs)
        
        if self.pbs:
            self.pbs.run(command=self.command, threads=self.threads, name="lexicon", memory=8, logfile="lexicon.log")
        else:
            run_command(self.command)

        self.source_target = compressed_file(self.source_target)
        self.target_source = compressed_file(self.target_source)

class Extract:
    def __init__(self, max_malloc=8, threads=4, mpi=None, pbs=None, makedirs=""):
        self.threads = threads
        self.max_malloc = max_malloc
        self.mpi = mpi
        self.pbs = pbs
        self.makedirs = makedirs
        self.data = []
        self.logfile = ""
        self.name = ""
        
        if not hasattr(self, 'command'):
            self.command = ""
        
    def run(self):
        for file in self.data:
            if not os.path.exists(file):
                raise ValueError, "no file: " + file

        if not os.path.exists(self.makedirs):
            os.makedirs(self.makedirs)

        if not self.name:
            self.name = "extract"

        if self.mpi:
            if self.pbs:
                self.pbs.run(command=self.command, name=self.name, mpi=self.mpi, memory=self.max_malloc, logfile=self.logfile)
            else:
                self.mpi.run(self.command)
        else:
            if self.pbs:
                self.pbs.run(command=self.command, threads=self.threads, name="extract", memory=self.max_malloc, logfile=self.logfile)
            else:
                run_command(self.command)

class ExtractPhrase(Extract):
    
    def __init__(self, cicada=None, corpus=None, alignment=None,
                 model_dir="",
                 max_length=7, max_fertility=4,
                 max_malloc=8, threads=4, mpi=None, pbs=None,
                 debug=None):
        Extract.__init__(self, max_malloc, threads, mpi, pbs, model_dir)
        
        self.counts = os.path.join(model_dir, "phrase-counts")

        self.data.append(corpus.source)
        self.data.append(corpus.target)
        self.logfile = "extract-phrase.log"
        self.name = "extract-phrase"
        
        prog_name = cicada.cicada_extract_phrase
        if mpi:
            prog_name = cicada.cicada_extract_phrase_mpi
        
        command = prog_name
        
        command += " --source \"%s\"" %(corpus.source)
        command += " --target \"%s\"" %(corpus.target)
        command += " --alignment \"%s\"" %(alignment.alignment)
        
        command += " --output \"%s\"" %(self.counts)
        
        command += " --max-length %d"    %(max_length)
        command += " --max-fertility %d" %(max_fertility)
        
        command += " --max-malloc %g" %(max_malloc)
        
        if not mpi:
            command += " --threads %d" %(threads)

        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"
        
        self.command = command

class ExtractSCFG(Extract):
    
    def __init__(self, cicada=None, corpus=None, alignment=None,
                 model_dir="",
                 max_length=7, max_fertility=4,
                 max_span_source=15, max_span_target=20,
                 min_hole_source=1, min_hole_target=1,
                 exhaustive=None, constrained=None, ternary=None, sentential=None,
                 max_malloc=8, threads=4, mpi=None, pbs=None,
                 debug=None):
        Extract.__init__(self, max_malloc, threads, mpi, pbs, model_dir)
        
        self.counts = os.path.join(model_dir, "scfg-counts")
        
        self.data.append(corpus.source)
        self.data.append(corpus.target)
        self.logfile = "extract-scfg.log"
        self.name = "extract-scfg"
        
        if os.path.exists(corpus.source_span) and os.path.exists(corpus.target_span):
            raise ValueError, "both of source/target span specified... which one?"
        
        prog_name = cicada.cicada_extract_scfg
        if mpi:
            prog_name = cicada.cicada_extract_scfg_mpi
        
        command = prog_name
        
        command += " --source \"%s\"" %(corpus.source)
        command += " --target \"%s\"" %(corpus.target)
        command += " --alignment \"%s\"" %(alignment.alignment)

        if os.path.exists(corpus.source_span):
            command += " --spans-source \"%s\"" %(corpus.source_span)
        if os.path.exists(corpus.target_span):
            command += " --spans-target \"%s\"" %(corpus.target_span)
        
        command += " --output \"%s\"" %(self.counts)
        
        command += " --max-length %d"    %(max_length)
        command += " --max-fertility %d" %(max_fertility)
        command += " --max-span-source %d"      %(max_span_source)
        command += " --max-span-target %d"      %(max_span_target)
        command += " --min-hole-source %d"      %(min_hole_source)
        command += " --min-hole-target %d"      %(min_hole_target)
        
        if exhaustive:
            command += " --exhaustive"
        if constrained:
            command += " --constrained"
        if ternary:
            command += " --ternary"
        if sentential:
            command += " --sentential"
        
        command += " --max-malloc %g" %(max_malloc)

        if not mpi:
            command += " --threads %d" %(self.threads)        
        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"
        
        self.command = command

class ExtractGHKM(Extract):
    
    def __init__(self, cicada=None, corpus=None, alignment=None,
                 model_dir="",
                 non_terminal="", max_sentence_length=0, max_nodes=15, max_height=4, max_compose=0, max_scope=0,
                 collapse_source=None,
                 collapse_target=None,
                 exhaustive=None,
                 constrained=None,
                 project=None,
                 max_malloc=8, threads=4, mpi=None, pbs=None,
                 debug=None):
        Extract.__init__(self, max_malloc, threads, mpi, pbs, model_dir)
        
        self.counts = os.path.join(model_dir, "ghkm-counts")

        if os.path.exists(corpus.source_forest) and os.path.exists(corpus.target_forest):
            raise ValueError, "both source and target forest.. we can extract string-to-tree or tree-to-string"
        
        tree_to_string = 1
        if os.path.exists(corpus.target_forest):
            tree_to_string = None
            
        if tree_to_string:
            self.data.append(corpus.source_forest)
            self.data.append(corpus.target)
        else:
            self.data.append(corpus.target_forest)
            self.data.append(corpus.source)
            
        self.logfile = "extract-ghkm.log"
        self.name = "extract-ghkm"
        
        prog_name = cicada.cicada_extract_ghkm
        if mpi:
            prog_name = cicada.cicada_extract_ghkm_mpi
        
        command = prog_name
        
        if tree_to_string:
            command += " --source \"%s\"" %(corpus.source_forest)
            command += " --target \"%s\"" %(corpus.target)
        else:
            ## strig-to-tree extraction...!
            command += " --source \"%s\"" %(corpus.target_forest)
            command += " --target \"%s\"" %(corpus.source)
            command += " --inverse"
            command += " --swap"
            
        command += " --alignment \"%s\"" %(alignment.alignment)
        
        command += " --output \"%s\"" %(self.counts)
        
        if non_terminal:
            if non_terminal[0] != '[' or non_terminal[-1] != ']':
                raise ValueError, "invalid non-terminal: %s" %(non_terminal)

            command += " --non-terminal \"%s\"" %(non_terminal)

        if max_sentence_length > 0:
            command += " --max-sentence-length %d"   %(max_sentence_length)
        
        command += " --max-nodes %d"   %(max_nodes)
        command += " --max-height %d"  %(max_height)
        command += " --max-compose %d" %(max_compose)
        command += " --max-scope %d"   %(max_scope)
        
        if collapse_source:
            command += " --collapse-source"
        if collapse_target:
            command += " --collapse-target"
        if exhaustive:
            command += " --exhaustive"
        if constrained:
            command += " --constrained"
        if project:
            command += " --project"
        
        command += " --max-malloc %g" %(max_malloc)

        if not mpi:
            command += " --threads %d" %(self.threads)
        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"

        self.command = command


class ExtractTree(Extract):
    
    def __init__(self, cicada=None, corpus=None, alignment=None,
                 model_dir="",
                 max_sentence_length=0, max_nodes=15, max_height=4, max_compose=0, max_scope=0,
                 collapse_source=None,
                 collapse_target=None,
                 exhaustive=None,
                 constrained=None,
                 max_malloc=8, threads=4, mpi=None, pbs=None,
                 debug=None):
        Extract.__init__(self, max_malloc, threads, mpi, pbs, model_dir)
        
        self.counts = os.path.join(model_dir, "tree-counts")

        self.data.append(corpus.source_forest)
        self.data.append(corpus.target_forest)
        self.logfile = "extract-tree.log"
        self.name = "extract-tree"
        
        prog_name = cicada.cicada_extract_tree
        if mpi:
            prog_name = cicada.cicada_extract_tree_mpi
        
        command = prog_name
        
        command += " --source \"%s\"" %(corpus.source_forest)
        command += " --target \"%s\"" %(corpus.target_forest)
        command += " --alignment \"%s\"" %(alignment.alignment)
        
        command += " --output \"%s\"" %(self.counts)
        
        command += " --max-nodes %d"   %(max_nodes)
        command += " --max-height %d"  %(max_height)
        command += " --max-compose %d" %(max_compose)
        command += " --max-scope %d"   %(max_scope)
        
        if collapse_source:
            command += " --collapse-source"
        if collapse_target:
            command += " --collapse-target"
        if exhaustive:
            command += " --exhaustive"
        if constrained:
            command += " --constrained"
        
        command += " --max-malloc %g" %(max_malloc)
        
        if not mpi:
            command += " --threads %d" %(self.threads)
            
        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"
        
        self.command = command

class ExtractScore(Extract):
    
    def __init__(self, cicada=None, lexicon=None,
                 model_dir="",
                 phrase=None, scfg=None, ghkm=None, tree=None,
                 max_malloc=8, threads=4, mpi=None, pbs=None,
                 debug=None):
        Extract.__init__(self, max_malloc, threads, mpi, pbs, model_dir)
        
        option = ""
        if phrase:
            self.counts = os.path.join(model_dir, "phrase-counts")
            self.scores = os.path.join(model_dir, "phrase-score")
            self.logfile = "extract-score.phrase.log"
            option = " --score-phrase"
        elif scfg:
            self.counts = os.path.join(model_dir, "scfg-counts")
            self.scores = os.path.join(model_dir, "scfg-score")
            self.logfile = "extract-score.scfg.log"
            option = " --score-scfg"
        elif ghkm:
            self.counts = os.path.join(model_dir, "ghkm-counts")
            self.scores = os.path.join(model_dir, "ghkm-score")
            self.logfile = "extract-score.ghkm.log"
            option = " --score-ghkm"
        elif tree:
            self.counts = os.path.join(model_dir, "tree-counts")
            self.scores = os.path.join(model_dir, "tree-score")
            self.logfile = "extract-score.tree.log"
            option = " --score-ghkm"
        else:
            raise ValueError, "no count type?"

        if not os.path.exists(self.counts):
            raise ValueError, "no counts? %s" %(self.counts)
                
        prog_name = cicada.cicada_extract_score
        if mpi:
            prog_name = cicada.cicada_extract_score_mpi
        
        command = prog_name
        
        command += " --input \"%s\"" %(self.counts)
        command += " --output \"%s\"" %(self.scores)
        command += " --lexicon-source-target \"%s\"" %(lexicon.source_target)
        command += " --lexicon-target-source \"%s\"" %(lexicon.target_source)
        command += option
        command += " --max-malloc %g" %(max_malloc)
        
        if mpi:
            command += " --prog \"%s\"" %(prog_name)
        else:
            command += " --threads %d" %(self.threads)
            
        if debug:
            command += " --debug=%d" %(debug)
        else:
            command += " --debug"

        self.command = command


(options, args) = opt_parser.parse_args()

if options.root_dir:
    if not os.path.exists(options.root_dir):
	os.makedirs(options.root_dir)

if not options.model_dir:
    options.model_dir = os.path.join(options.root_dir, "model")
if not options.lexical_dir:
    options.lexical_dir = options.model_dir
if not options.alignment_dir:
    options.alignment_dir = options.model_dir

cicada = CICADA(options.cicada_dir)

mpi = None
if options.mpi_host or options.mpi_host_file or options.mpi > 0:
    mpi = MPI(dir=options.mpi_dir,
              hosts=options.mpi_host,
              hosts_file=options.mpi_host_file,
              number=options.mpi)

pbs = None
if options.pbs:
    pbs = PBS(queue=options.pbs_queue)

corpus = Corpus(corpus=options.corpus,
                f=options.f,
                e=options.e,
                sf=options.sf,
                se=options.se,
                ff=options.ff,
                fe=options.fe)

alignment = Alignment(options.alignment_dir, options.alignment)

lexicon = Lexicon(cicada=cicada, corpus=corpus, alignment=alignment,
                  lexical_dir=options.lexical_dir,
                  prior=options.lexicon_prior,
                  inverse=options.lexicon_inverse,
                  threads=options.threads, mpi=mpi, pbs=pbs,
                  debug=options.debug)

if options.first_step <= 4 and options.last_step >= 4:
    print "(4) generate lexical translation table started  @", time.ctime()
    lexicon.run()
    print "(4) generate lexical translation table finished @", time.ctime()

if options.first_step <= 5 and options.last_step >= 5:
    extract = None
    if options.phrase:
        extract = ExtractPhrase(cicada=cicada, corpus=corpus, alignment=alignment,
                                model_dir=options.model_dir,
                                max_length=options.max_length,
                                max_fertility=options.max_fertility,
                                max_malloc=options.max_malloc, threads=options.threads, mpi=mpi, pbs=pbs,
                                debug=options.debug)
    elif options.scfg:
        extract = ExtractSCFG(cicada=cicada, corpus=corpus, alignment=alignment,
                              model_dir=options.model_dir,
                              max_length=options.max_length,
                              max_fertility=options.max_fertility,
                              max_span_source=options.max_span_source,
                              max_span_target=options.max_span_target,
                              min_hole_source=options.min_hole_source,
                              min_hole_target=options.min_hole_target,
                              exhaustive=options.exhaustive,
                              constrained=options.constrained,
                              ternary=options.ternary,
                              sentential=options.sentential,
                              max_malloc=options.max_malloc, threads=options.threads, mpi=mpi, pbs=pbs,
                              debug=options.debug)
    elif options.ghkm:
        extract = ExtractGHKM(cicada=cicada, corpus=corpus, alignment=alignment,
                              model_dir=options.model_dir,
                              non_terminal=options.non_terminal,
                              max_sentence_length=options.max_sentence_length,
                              max_nodes=options.max_nodes,
                              max_height=options.max_height,
                              max_compose=options.max_compose,
                              max_scope=options.max_scope,
                              collapse_source=options.collapse_source,
                              collapse_target=options.collapse_target,
                              exhaustive=options.exhaustive,
                              constrained=options.constrained,
                              project=options.project,
                              max_malloc=options.max_malloc, threads=options.threads, mpi=mpi, pbs=pbs,
                              debug=options.debug)
    elif options.tree:
        extract = ExtractTree(cicada=cicada, corpus=corpus, alignment=alignment,
                              model_dir=options.model_dir,
                              max_sentence_length=options.max_sentence_length,
                              max_nodes=options.max_nodes,
                              max_height=options.max_height,
                              max_compose=options.max_compose,
                              max_scope=options.max_scope,
                              collapse_source=options.collapse_source,
                              collapse_target=options.collapse_target,
                              exhaustive=options.exhaustive,
                              constrained=options.constrained,
                              max_malloc=options.max_malloc, threads=options.threads, mpi=mpi, pbs=pbs,
                              debug=options.debug)
    else:
        raise ValueError, "no count type?"

    print "(5) extract phrase table started @", time.ctime()
    extract.run()
    print "(5) extract phrase table finished @", time.ctime()

if options.first_step <= 6 and options.last_step >= 6:
    score = ExtractScore(cicada=cicada, lexicon=lexicon,
                         model_dir=options.model_dir,
                         phrase=options.phrase, scfg=options.scfg, ghkm=options.ghkm, tree=options.tree,
                         max_malloc=options.max_malloc, threads=options.threads, mpi=mpi, pbs=pbs,
                         debug=options.debug)
    
    print "(6) score phrase table started @", time.ctime()
    score.run()
    
    print "(6) score phrase table finished @", time.ctime()
