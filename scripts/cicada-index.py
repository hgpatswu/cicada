#!/usr/bin/env python
#
#  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
#
###
### a wrapper script for indexing models, such as grammar, rule-grammar, word-clusters and global-lexicon
### Currently, we support only grammar and tree-grammar
###

import threading

import time
import sys
import os, os.path
import string
import re
import subprocess
import shutil

import UserList
import UserString
import cStringIO

from optparse import OptionParser, make_option

opt_parser = OptionParser(
    option_list=[
    # output directory/filename prefix
    make_option("--root-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="root directory for outputs"),
    make_option("--model-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="model directory (default: ${root_dir}/model)"),
    make_option("--lexical-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="lexical transltion table directory (default: ${model_dir})"),
    make_option("--counts-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="grammar counts directory (default: ${model_dir})"),
    make_option("--score-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="grammar score directory (default: ${model_dir})"),
    make_option("--index-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="grammar index directory (default: ${model_dir})"),

    make_option("--temporary-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="temporary directory"),

    make_option("--lexicon-source-target", default="", action="store", type="string",
                metavar="LEXICON", help="lexicon for P(target | source) (default: ${lexical_dir}/lex.f2n)"),
    make_option("--lexicon-target-source", default="", action="store", type="string",
                metavar="LEXICON", help="lexicon for P(source | target) (default: ${lexical_dir}/lex.n2f)"),

    ## smoothing...
    make_option("--prior", default=0.1, action="store", type="float", metavar="PRIOR", help="model prior (default: %default)"),
    
    ## feature/attribute names
    make_option("--feature",   default=[], action="append", type="string", help="feature definitions"),
    make_option("--attribute", default=[], action="append", type="string", help="attribute definitions"),
    
    ### options...
    make_option("--phrase", default=None, action="store_true", help="index phrase grammar"),
    make_option("--scfg",   default=None, action="store_true", help="index SCFG grammar"),
    make_option("--ghkm",   default=None, action="store_true", help="index GHKM (tree-to-string) grammar"),
    make_option("--tree",   default=None, action="store_true", help="index tree-to-tree grammar"),
    
    make_option("--cky",    default=None, action="store_true", help="CKY mode indexing for tree-grammar"),
    make_option("--reordering", default=None, action="store_true", help="reordering for phrase grammar"),
    
    ## additional feature functions
    make_option("--feature-root",               default=None, action="store_true", help="generative probability"),
    make_option("--feature-fisher",             default=None, action="store_true", help="Fisher's exact test"),
    make_option("--feature-type",               default=None, action="store_true", help="observation probability"),
    make_option("--feature-singleton",          default=None, action="store_true", help="singleton features"),
    make_option("--feature-cross",              default=None, action="store_true", help="cross features"),
    make_option("--feature-unaligned",          default=None, action="store_true", help="unaligned features"),
    make_option("--feature-internal",           default=None, action="store_true", help="internal features"),
    make_option("--feature-height",             default=None, action="store_true", help="height features"),

    make_option("--feature-lexicon",            default=None, action="store_true", help="Lexical features"),
    make_option("--feature-model1",             default=None, action="store_true", help="Model1 features"),
    make_option("--feature-noisy-or",           default=None, action="store_true", help="noisy-or features"),
    make_option("--feature-insertion-deletion", default=None, action="store_true", help="insertion/deletion features"),

    make_option("--prefix-feature",   default="", action="store", type="string", help="feature name prefix (default: %default)"),
    make_option("--prefix-attribute", default="", action="store", type="string", help="attribute name prefix (default: %default)"),
    
    make_option("--threshold-insertion", default=0.5, action="store", type="float", help="threshold for insertion (default: %default)"),
    make_option("--threshold-deletion",  default=0.5, action="store", type="float", help="threshold for deletion (default: %default)"),
    
    ## quantize
    make_option("--quantize", default=None, action="store_true", help="perform quantization"),
    
    ## kbest options
    make_option("--kbest", default=0, action="store", type="int",
                metavar="KBEST", help="kbest max count of rules (default: %default)"),
    make_option("--kbest-count",  default=None, action="store_true",  help="kbest option: use count for sorting"),
    make_option("--kbest-joint",  default=None, action="store_true",  help="kbest option: use joint probability for sorting"),
    make_option("--kbest-source",  default=None, action="store_true", help="kbest option: use source probability (P(f|e)) for sorting"),
    make_option("--kbest-target",  default=None, action="store_true", help="kbest option: use target probability (P(e|f)) for sorting"),
    make_option("--cutoff", default=0, action="store", type="float",
                metavar="CUTOFF", help="cutoff count of rules (default: %default)"),
    make_option("--threshold", default=0, action="store", type="float",
                metavar="THRESHOLD", help="probability threshold of rules (default: %default)"),
    make_option("--sigtest", default=0, action="store", type="float",
                metavar="SIGTEST", help="significance testing threshold relative to 1-1-1-N log-p-value (or \epsilon in \"discarding most of the phrasetable\") (default: %default)"),
    make_option("--sigtest-inclusive", default=None, action="store_true", 
                help="significance testing which includes 1-1-1-N event (this will assign --sigtest -0.001)"),
    make_option("--sigtest-exclusive", default=None, action="store_true", 
                help="significance testing which excludes 1-1-1-N event (this will assign --sigtest +0.001)"),

    ## max-malloc
    make_option("--max-malloc", default=8, action="store", type="float",
                metavar="MALLOC", help="maximum memory in GB (default: %default)"),

    # CICADA Toolkit directory
    make_option("--cicada-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="cicada directory"),
    # MPI Implementation.. if different from standard location...
    make_option("--mpi-dir", default="", action="store", type="string",
                metavar="DIRECTORY", help="MPI directory"),

    make_option("--threads", default=1, action="store", type="int",
                help="# of thrads for thread-based parallel processing"),
    # perform threading or MPI training    
    make_option("--mpi", default=0, action="store", type="int",
                help="# of processes for MPI-based parallel processing. Identical to --np for mpirun"),
    make_option("--mpi-host", default="", action="store", type="string",
                help="list of hosts to run job. Identical to --host for mpirun", metavar="HOSTS"),
    make_option("--mpi-host-file", default="", action="store", type="string",
                help="host list file to run job. Identical to --hostfile for mpirun", metavar="FILE"),
    make_option("--mpi-options", default="", action="store", type="string",
                metavar="OPTION", help="additional MPI options"),    
    make_option("--pbs", default=None, action="store_true",
                help="PBS for launching processes"),
    make_option("--pbs-queue", default="", action="store", type="string",
                help="PBS queue for launching processes (default: %default)", metavar="NAME"),

    ## debug messages
    make_option("--debug", default=0, action="store", type="int"),
    ])

def find_executable(executable, paths=[]):
    ### taken from distutils.spawn
    
    paths += os.environ['PATH'].split(os.pathsep)
    
    base, ext = os.path.splitext(executable)

    if (sys.platform.startswith('win') or sys.platform.startswith('os2')) and (ext != '.exe'):
        executable = executable + '.exe'

    if not os.path.isfile(executable):
        for p in paths:
            f = os.path.join(p, executable)
            if os.path.isfile(f):
                # the file exists, we have a shot at spawn working
                return f
        return None
    else:
        return executable

def run_command(command):
    try:
        retcode = subprocess.call(command, shell=True)
        if retcode:
            sys.exit(retcode)
    except:
        raise ValueError, "subprocess.call failed: %s" %(command)

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

class Quoted:
    def __init__(self, arg):
        self.arg = arg
        
    def __str__(self):
        return '"' + str(self.arg) + '"'

class Option:
    def __init__(self, arg, value=None):
        self.arg = arg
        self.value = value

    def __str__(self,):
        option = self.arg
        
        if self.value is not None:
            if isinstance(self.value, int):
                option += " %d" %(self.value)
            elif isinstance(self.value, long):
                option += " %d" %(self.value)
            elif isinstance(self.value, float):
                option += " %.20g" %(self.value)
            else:
                option += " %s" %(str(self.value))
        return option

class Program:
    def __init__(self, *args):
        self.args = list(args[:])

    def __str__(self,):
        return ' '.join(map(str, self.args))
    
    def __iadd__(self, other):
        self.args.append(other)
        return self

class QSUB:
    def __init__(self, script=""):
        self.script = script
        self.qsub = find_executable('qsub')
        
        if not self.qsub:
            raise ValueError, "no qsub in your executable path?"

    def start(self):
        self.run()

    def join(self):
        self.wait()
        
    ### actual implementations
    def run(self):
        self.popen = subprocess.Popen([self.qsub, '-S', '/bin/sh'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        
        ### reader therad
        self.stdout = threading.Thread(target=self._reader, args=[self.popen.stdout])
        self.stdout.start()
        
        ### feed script
        self.popen.stdin.write(self.script)
        self.popen.stdin.close()
        
    def wait(self):
        self.stdout.join()
        self.popen.wait()

    def _reader(self, fh):
        fh.read()
        
class PBS:
    def __init__(self, queue=""):
        self.workers = []
        self.queue = queue

    def wait(self):
        for worker in self.workers:
            worker.join()
            
    def run(self, command="", threads=1, memory=0.0, name="name", logfile=None):
        pipe = cStringIO.StringIO()
        
        pipe.write("#!/bin/sh\n")
        pipe.write("#PBS -S /bin/sh\n")
        pipe.write("#PBS -N %s\n" %(name))
        pipe.write("#PBS -e localhost:/dev/null\n")
        pipe.write("#PBS -o localhost:/dev/null\n")
        pipe.write("#PBS -W block=true\n")
        
        if self.queue:
            pipe.write("#PBS -q %s\n" %(self.queue))
        
        mem = ""
        if memory >= 1.0:
            mem=":mem=%dgb" %(int(memory))
        elif memory >= 0.001:
            mem=":mem=%dmb" %(int(memory * 1000))
        elif memory >= 0.000001:
            mem=":mem=%dkb" %(int(memory * 1000 * 1000))

        pipe.write("#PBS -l select=1:ncpus=%d:mpiprocs=1%s\n" %(threads, mem))
        
        # setup variables
        if os.environ.has_key('TMPDIR_SPEC'):
            pipe.write("export TMPDIR_SPEC=%s\n" %(os.environ['TMPDIR_SPEC']))
        if os.environ.has_key('LD_LIBRARY_PATH'):
            pipe.write("export LD_LIBRARY_PATH=%s\n" %(os.environ['LD_LIBRARY_PATH']))
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            pipe.write("export DYLD_LIBRARY_PATH=%s\n" %(os.environ['DYLD_LIBRARY_PATH']))
            
        pipe.write("if test \"$PBS_O_WORKDIR\" != \"\"; then\n")
        pipe.write("  cd $PBS_O_WORKDIR\n")
        pipe.write("fi\n")
        
        if logfile:
            pipe.write("%s 2> %s\n" %(command, logfile))
        else:
            pipe.write("%s\n" %(command))
            
        self.workers.append(QSUB(pipe.getvalue()))
        self.workers[-1].start();

class Threads:
    
    def __init__(self, cicada=None, threads=1):
        self.popen = subprocess.Popen([cicada.thrsh, '--threads', str(threads)], stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def wait(self):
        self.pipe.close()
        self.popen.wait()

    def run(self, command="", logfile=None):
        if logfile:
            self.pipe.write("%s 2> %s\n" %(command, logfile))
        else:
            self.pipe.write("%s\n" %(command))
        self.pipe.flush()

class MPI:
    
    def __init__(self, cicada=None, dir="", hosts="", hosts_file="", number=0, options=""):
        
	self.dir = dir
	self.hosts = hosts
        self.hosts_file = hosts_file
        self.number = number
        self.options = options
	
        if self.dir:
            if not os.path.exists(self.dir):
                raise ValueError, self.dir + " does not exist"
            self.dir = os.path.realpath(self.dir)

        if self.hosts_file:
            if not os.path.exists(self.hosts_file):
                raise ValueError, self.hosts_file + " does no exist"
            self.hosts_file = os.path.realpath(hosts_file)

        self.bindir = self.dir
	
        paths = []
        if self.bindir:
            paths = [os.path.join(self.bindir, 'bin'), self.bindir]
        
        binprog = find_executable('openmpirun', paths)
        if not binprog:
            binprog = find_executable('mpirun', paths)

        if not binprog:
            raise ValueError, "no openmpirun nor mpirun?"

        setattr(self, 'mpirun', binprog)
        
        command = self.mpirun
        if self.number > 0:
            command += ' --np %d' %(self.number)
        if self.hosts:
            command += ' --host %s' %(self.hosts)
        elif self.hosts_file:
            command += ' --hostfile %s' %(self.hosts_file)

        if os.environ.has_key('TMPDIR_SPEC'):
            command += ' -x TMPDIR_SPEC'
        if os.environ.has_key('LD_LIBRARY_PATH'):
            command += ' -x LD_LIBRARY_PATH'
        if os.environ.has_key('DYLD_LIBRARY_PATH'):
            command += ' -x DYLD_LIBRARY_PATH'

        command += ' ' + self.options
        
        command += " %s" %(cicada.mpish)
        
        self.popen = subprocess.Popen(command, shell=True, stdin=subprocess.PIPE)
        self.pipe = self.popen.stdin
        
    def wait(self):
        self.pipe.close()
        self.popen.wait()
        
    def run(self, command="", logfile=None):
        if logfile:
            self.pipe.write("%s 2> %s\n" %(command, logfile))
        else:
            self.pipe.write("%s\n" %(command))
        self.pipe.flush()


class CICADA:
    def __init__(self, dir=""):
        bindirs = []
        
        if not dir:
            dir = os.path.abspath(os.path.dirname(__file__))
            bindirs.append(dir)
            parent = os.path.dirname(dir)
            if parent:
                dir = parent
        else:
            dir = os.path.realpath(dir)
            if not os.path.exists(dir):
                raise ValueError, dir + " does not exist"
            bindirs.append(dir)
        
	for subdir in ('bin', 'progs', 'scripts'): 
	    bindir = os.path.join(dir, subdir)
	    if os.path.exists(bindir) and os.path.isdir(bindir):
		bindirs.append(bindir)
	
        for binprog in ('cicada_index_grammar',
                        'cicada_index_tree_grammar',
                        ### indexers
                        'cicada_filter_extract',
                        'cicada_filter_extract_phrase',
                        'cicada_filter_extract_scfg',
                        'cicada_filter_extract_ghkm',
                        ### misc...
                        'cicada_filter_tee',
                        ### filters
                        'mpish', ### mpi-launcher
                        'thrsh', ### thread-launcher
                        ### launchers
                        ):

            prog = find_executable(binprog, bindirs)
            if not prog:
                raise ValueError, binprog + ' does not exist'
                
            setattr(self, binprog, prog)

class IndexPhrase:
    def __init__(self,
                 cicada=None,
                 counts_dir="",
                 score_dir="",
                 index_dir="",
                 cky=None,
                 reordering=None):
        self.indexer = cicada.cicada_index_grammar
        self.filter  = cicada.cicada_filter_extract_phrase
        self.filter += " --cicada"
        
        if reordering:
            self.filter += " --reordering --bidirectional"

        self.cky = None
        self.grammar = "grammar"
        self.name = "phrase"
        
        self.counts = os.path.join(counts_dir, "phrase-counts")
        self.scores = os.path.join(score_dir, "phrase-score")
        self.index  = os.path.join(index_dir, "phrase-index")
        self.base   = index_dir
        
class IndexSCFG:
    def __init__(self,
                 cicada=None,
                 counts_dir="",
                 score_dir="",
                 index_dir="",
                 cky=None,
                 reordering=None):
        self.indexer = cicada.cicada_index_grammar
        self.filter  = cicada.cicada_filter_extract_scfg
        self.cky = None
        self.grammar = "grammar"
        self.name = "scfg"

        self.counts = os.path.join(counts_dir, "scfg-counts")
        self.scores = os.path.join(score_dir, "scfg-score")
        self.index  = os.path.join(index_dir, "scfg-index")
        self.base   = index_dir

class IndexGHKM:
    def __init__(self,
                 cicada=None,
                 counts_dir="",
                 score_dir="",
                 index_dir="",
                 cky=None, 
                 reordering=None):
        self.indexer = cicada.cicada_index_tree_grammar
        self.filter  = cicada.cicada_filter_extract_ghkm
        self.cky = cky
        self.grammar = "tree-grammar"
        self.name = "ghkm"

        self.counts = os.path.join(counts_dir, "ghkm-counts")
        self.scores = os.path.join(score_dir, "ghkm-score")
        self.index  = os.path.join(index_dir, "ghkm-index")
        self.base   = index_dir

class IndexTree:
    def __init__(self,
                 cicada=None,
                 counts_dir="",
                 score_dir="",
                 index_dir="",
                 cky=None,
                 reordering=None):
        self.indexer = cicada.cicada_index_tree_grammar
        self.filter  = cicada.cicada_filter_extract_ghkm
        self.cky = cky
        self.grammar = "tree-grammar"
        self.name = "tree"

        self.counts = os.path.join(counts_dir, "tree-counts")
        self.scores = os.path.join(score_dir, "tree-score")
        self.index  = os.path.join(index_dir, "tree-index")
        self.base   = index_dir

## additional features...
class Features:
    def __init__(self,
                 root=None,
                 fisher=None,
                 types=None,
                 singleton=None,
                 cross=None,
                 unaligned=None,
                 internal=None,
                 height=None):
        self.root      = root
        self.fisher    = fisher
        self.types     = types
        self.singleton = singleton
        self.cross     = cross
        self.unaligned = unaligned
        self.internal  = internal
        self.height    = height

        self.options = ""
        
        if root:
            self.options += " --feature-root"
        if fisher:
            self.options += " --feature-fisher"
        if types:
            self.options += " --feature-type"
        if singleton:
            self.options += " --feature-singleton"
        if cross:
            self.options += " --feature-cross"
        if unaligned:
            self.options += " --feature-unaligned"
        if internal:
            self.options += " --feature-internal"
        if height:
            self.options += " --feature-height"

class Lexicon:
    def __init__(self,
                 lexicon_source_target="",
                 lexicon_target_source="",
                 lexicon=None,
                 model1=None,
                 noisy_or=None,
                 insertion_deletion=None,
                 threshold_insertion=0.1,
                 threshold_deletion=0.1):
        self.lexicon_source_target = compressed_file(lexicon_source_target)
        self.lexicon_target_source = compressed_file(lexicon_target_source)
        
        self.lexicon  = lexicon
        self.model1   = model1
        self.noisy_or = noisy_or
        self.insertion_deletion = insertion_deletion
        
        self.threshold_insertion = threshold_insertion
        self.threshold_deletion  = threshold_deletion

        self.options = ""
        
        if lexicon or model1 or noisy_or or insertion_deletion:
            self.options += " --lexicon-source-target \"%s\"" %(self.lexicon_source_target)
            self.options += " --lexicon-target-source \"%s\"" %(self.lexicon_target_source)

            if lexicon:
                self.options += " --feature-lexicon"
            if model1:
                self.options += " --feature-model1"
            if noisy_or:
                self.options += " --feature-noisy-or"
            if insertion_deletion:
                self.options += " --feature-insertion-deletion"
                self.options += " --threshold-insertion %.20g" %(self.threshold_insertion)
                self.options += " --threshold-deletion %.20g" %(self.threshold_deletion)


class Index(UserString.UserString):
    def __init__(self,
                 cicada=None,
                 indexer=None,
                 lexicon=None,
                 feats=None,
                 prefix_feature="",
                 prefix_attribute="",
                 input="",
                 output="",
                 plain="",
                 name="",
                 root_joint="",
                 root_source="",
                 root_target="",
                 temporary_dir="",
                 prior=0.1,
                 kbest=0,
                 kbest_count=None,
                 kbest_joint=None,
                 kbest_source=None,
                 kbest_target=None,
                 cutoff=0.0,
                 threshold=0.0,
                 sigtest=0.0,
                 quantize=None,
                 features=[],
                 attributes=[]):

        if not input:
            raise ValueError, "no input? %s" %(input)
        if not output:
            raise ValueError, "no output? %s" %(output)
        if not plain:
            raise ValueError, "no output? %s" %(plain)
        if not root_joint:
            raise ValueError, "no root count? %s" %(root_joint)
        if not root_source:
            raise ValueError, "no root source? %s" %(root_source)
        if not root_target:
            raise ValueError, "no root target? %s" %(root_target)
        
        stat_file = os.path.join(indexer.counts, "statistics")

        self.name    = indexer.name + "-index"
        self.logfile = os.path.join(indexer.base, indexer.name + "-index." + name + ".log")
        
        command = ""

        if kbest > 0 or cutoff > 0.0 or threshold > 0.0 or sigtest != 0.0:
            self.threads = 2

            command = cicada.cicada_filter_extract
            
            if kbest > 0:
                command += " --kbest %d" %(kbest)
                
                if kbest_count:
                    command += " --kbest-count"
                if kbest_joint:
                    command += " --kbest-joint"
                if kbest_source:
                    command += " --kbest-source"
                if kbest_target:
                    command += " --kbest-target"                    
                
            if cutoff > 0.0:
                command += " --cutoff %g" %(cutoff)
            if threshold > 0.0:
                command += " --threshold %g" %(threshold)
            if sigtest != 0.0:
                if not os.path.exists(stat_file):
                    raise ValueError, "no statistics file for significant testing?" + stat_file

                command += " --sigtest %g" %(sigtest)
                command += " --statistic \"%s\"" %(stat_file)
                
            command += " --input \"%s\"" %(input)
            command += " --debug"
            
            command += " | "
            command += indexer.filter
            command += " --dirichlet-prior %g" %(prior)
            command += " --root-joint \"%s\""  %(root_joint)
            command += " --root-source \"%s\"" %(root_source)
            command += " --root-target \"%s\"" %(root_target)
            if os.path.exists(stat_file):
                command += " --statistic \"%s\"" %(stat_file)
            if feats:
                command += feats.options
            if lexicon:
                command += lexicon.options
            command += " --debug"
        else:
            
            self.threads = 1

            command = indexer.filter
            command += " --dirichlet-prior %g" %(prior)
            command += " --root-joint \"%s\""  %(root_joint)
            command += " --root-source \"%s\"" %(root_source)
            command += " --root-target \"%s\"" %(root_target)
            if os.path.exists(stat_file):
                command += " --statistic \"%s\"" %(stat_file)
            if feats:
                command += feats.options
            if lexicon:
                command += lexicon.options
            command += " --input \"%s\"" %(input)
            command += " --debug"
            
        ### actual indexer...
        command_indexer = indexer.indexer
        
        if quantize:
            command_indexer += " --quantize"

        input_data=[]

        if indexer.cky:
            input_data.append('cky=true')
        if prefix_feature:
            input_data.append("feature-prefix=" + prefix_feature)
        if prefix_attribute:
            input_data.append("attribute-prefix=" + prefix_attribute)
        if features:
            input_data += features
        if attributes:
            input_data += attributes
        
        # add debug flag
        input_data.append('debug=1')

        input_path = '-'
        if input_data:
            input_path += ':' + ','.join(input_data)
        
        command_indexer += " --input %s" %(input_path)
        command_indexer += " --output \"%s\"" %(output)
        
        if temporary_dir:
            command_indexer += " --temporary \"%s\"" %(temporary_dir)
            
        self.threads += 1
        
        # tee the content!
        command += " | " + cicada.cicada_filter_tee + " \"%s\"" %(plain)
        
        # actual indexing!
        command += " | " + command_indexer
        
        UserString.UserString.__init__(self, '('+command+')')
        
class Model:
    def __init__(self, input="", output="", plain="", name=""):
        self.input = input
        self.output = output
        self.plain = plain
        self.name = name


class Models(UserList.UserList):
    def __init__(self, indexer=None):

        UserList.UserList.__init__(self)

        prefix = indexer.scores
        output = indexer.index

        path_files = os.path.join(prefix, 'files');
        path_root_joint  = os.path.join(prefix, 'root-joint.gz')
        path_root_source = os.path.join(prefix, 'root-source.gz')
        path_root_target = os.path.join(prefix, 'root-target.gz')
        
        if not os.path.exists(path_files):
            raise ValueError, "no path to files: %s" %(path_files)
        if not os.path.exists(path_root_joint):
            raise ValueError, "no path to root-joint: %s" %(path_root_joint)
        if not os.path.exists(path_root_source):
            raise ValueError, "no path to root-source: %s" %(path_root_source)
        if not os.path.exists(path_root_target):
            raise ValueError, "no path to root-target: %s" %(path_root_target)
        
        self.root_joint  = path_root_joint
        self.root_source = path_root_source
        self.root_target = path_root_target
        
        if output:
            if os.path.exists(output):
                if not os.path.isdir(output):
                    os.remove(output)
                    os.makedirs(output)
            else:
                os.makedirs(output)
        
        for line in open(path_files):
            name = line.strip()
            if not name: continue
            
            path = os.path.join(prefix, name)
            if not os.path.exists(path):
                raise ValueError, "no path to scores: %s" %(path)

            root,stem = os.path.splitext(name)
            
            self.append(Model(path, os.path.join(output, root + '.bin'), os.path.join(output, root + '.gz'), root))
            
if __name__ == '__main__':
    (options, args) = opt_parser.parse_args()

    ### dump to stderr
    stdout = sys.stdout
    sys.stdout = sys.stderr

    ### setup defaults!
    
    if options.root_dir:
        if not os.path.exists(options.root_dir):
            os.makedirs(options.root_dir)

    if not options.model_dir:
        options.model_dir = os.path.join(options.root_dir, "model")
    if not options.lexical_dir:
        options.lexical_dir = options.model_dir
    if not options.counts_dir:
        options.counts_dir = options.model_dir
    if not options.score_dir:
        options.score_dir = options.model_dir
    if not options.index_dir:
        options.index_dir = options.model_dir
    if not os.path.exists(options.index_dir):
        os.makedirs(options.index_dir)

    if not options.lexicon_source_target:
        options.lexicon_source_target = os.path.join(options.lexical_dir, "lex.f2n")
    if not options.lexicon_target_source:
        options.lexicon_target_source = os.path.join(options.lexical_dir, "lex.n2f")

    if not options.temporary_dir:
        if os.environ.has_key('TMPDIR_SPEC') and os.environ['TMPDIR_SPEC']:
            options.temporary_dir = os.environ['TMPDIR_SPEC']
    else:
        os.environ['TMPDIR_SPEC'] = options.temporary_dir

    num_sigtest = 0
    if options.sigtest != 0.0:
        num_sigtest += 1
    if options.sigtest_inclusive:
        num_sigtest += 1
    if options.sigtest_exclusive:
        num_sigtest += 1

    if num_sigtest > 1:
        raise ValueError, "you can specify either one of --sigtest <\epsilon> or --sigtest-inclusive, --sigtest-exclusive"
        
    if options.sigtest_inclusive:
        options.sigtest = -0.001
    elif options.sigtest_exclusive:
        options.sigtest = 0.001

    cicada = CICADA(options.cicada_dir)

    indexer = None
    if options.phrase:
        indexer = IndexPhrase(cicada,
                              counts_dir=options.counts_dir,
                              score_dir=options.score_dir,
                              index_dir=options.index_dir,
                              cky=options.cky,
                              reordering=options.reordering)
    elif options.scfg:
        indexer = IndexSCFG(cicada,
                            counts_dir=options.counts_dir,
                            score_dir=options.score_dir,
                            index_dir=options.index_dir,
                            cky=options.cky,
                            reordering=options.reordering)
    elif options.ghkm:
        indexer = IndexGHKM(cicada,
                            counts_dir=options.counts_dir,
                            score_dir=options.score_dir,
                            index_dir=options.index_dir,
                            cky=options.cky,
                            reordering=options.reordering)
    elif options.tree:
        indexer = IndexTree(cicada,
                            counts_dir=options.counts_dir,
                            score_dir=options.score_dir,
                            index_dir=options.index_dir,
                            cky=options.cky,
                            reordering=options.reordering)
    else:
        raise ValueError, "no indexer?"

    models = Models(indexer)
    features = Features(root=options.feature_root,
                        fisher=options.feature_fisher,
                        types=options.feature_type,
                        singleton=options.feature_singleton,
                        cross=options.feature_cross,
                        unaligned=options.feature_unaligned,
                        internal=options.feature_internal,
                        height=options.feature_height)
    lexicon = Lexicon(lexicon_source_target=options.lexicon_source_target,
                      lexicon_target_source=options.lexicon_target_source,
                      lexicon=options.feature_lexicon,
                      model1=options.feature_model1,
                      noisy_or=options.feature_noisy_or,
                      insertion_deletion=options.feature_insertion_deletion,
                      threshold_insertion=options.threshold_insertion,
                      threshold_deletion=options.threshold_deletion)
    
    fp = open(os.path.join(indexer.index, "files"), 'w')

    if options.pbs:
        # we use pbs to run jobs
        pbs = PBS(queue=options.pbs_queue)
    
        for model in models:
            index = Index(cicada=cicada,
                          indexer=indexer,
                          lexicon=lexicon,
                          feats=features,
                          prefix_feature=options.prefix_feature,
                          prefix_attribute=options.prefix_attribute,
                          input=model.input,
                          output=model.output,
                          plain=model.plain,
                          name=model.name,
                          root_joint=models.root_joint,
                          root_source=models.root_source,
                          root_target=models.root_target,
                          temporary_dir=options.temporary_dir,
                          prior=options.prior,
                          kbest=options.kbest,
                          kbest_count=options.kbest_count,
                          kbest_joint=options.kbest_joint,
                          kbest_source=options.kbest_source,
                          kbest_target=options.kbest_target,
                          cutoff=options.cutoff,
                          threshold=options.threshold,
                          sigtest=options.sigtest,
                          quantize=options.quantize,
                          features=options.feature,
                          attributes=options.attribute)
            
            fp.write(os.path.basename(model.output)+'\n')

            print str(index), '2> %s'%(index.logfile)
            
            pbs.run(command=index, threads=index.threads, memory=options.max_malloc, name=index.name, logfile=index.logfile)
        pbs.wait()
    
    elif options.mpi:
        mpi = MPI(cicada=cicada,
                  dir=options.mpi_dir,
                  hosts=options.mpi_host,
                  hosts_file=options.mpi_host_file,
                  number=options.mpi,
                  options=options.mpi_options)
        
        for model in models:
            index = Index(cicada=cicada,
                          indexer=indexer,
                          lexicon=lexicon,
                          feats=features,
                          prefix_feature=options.prefix_feature,
                          prefix_attribute=options.prefix_attribute,
                          input=model.input,
                          output=model.output,
                          plain=model.plain,
                          name=model.name,
                          root_joint=models.root_joint,
                          root_source=models.root_source,
                          root_target=models.root_target,
                          temporary_dir=options.temporary_dir,
                          prior=options.prior,
                          kbest=options.kbest,
                          kbest_count=options.kbest_count,
                          kbest_joint=options.kbest_joint,
                          kbest_source=options.kbest_source,
                          kbest_target=options.kbest_target,
                          cutoff=options.cutoff,
                          threshold=options.threshold,
                          sigtest=options.sigtest,
                          quantize=options.quantize,
                          features=options.feature,
                          attributes=options.attribute)

            fp.write(os.path.basename(model.output)+'\n')

            print str(index), '2> %s'%(index.logfile)

            mpi.run(command=index, logfile=index.logfile)
        
        mpi.wait()
    else:
        threads = Threads(cicada=cicada, threads=options.threads)
    
        for model in models:
            index = Index(cicada=cicada,
                          indexer=indexer,
                          lexicon=lexicon,
                          feats=features,
                          prefix_feature=options.prefix_feature,
                          prefix_attribute=options.prefix_attribute,
                          input=model.input,
                          output=model.output,
                          plain=model.plain,
                          name=model.name,
                          root_joint=models.root_joint,
                          root_source=models.root_source,
                          root_target=models.root_target,
                          temporary_dir=options.temporary_dir,
                          prior=options.prior,
                          kbest=options.kbest,
                          kbest_count=options.kbest_count,
                          kbest_joint=options.kbest_joint,
                          kbest_source=options.kbest_source,
                          kbest_target=options.kbest_target,
                          cutoff=options.cutoff,
                          threshold=options.threshold,
                          sigtest=options.sigtest,
                          quantize=options.quantize,
                          features=options.feature,
                          attributes=options.attribute)
        
            fp.write(os.path.basename(model.output)+'\n')

            print str(index), '2> %s'%(index.logfile)

            threads.run(command=index, logfile=index.logfile)

        threads.wait()
