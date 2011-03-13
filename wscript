# wscript for Iris
#
# Currently the autotools build system is more complete and should be used for
# release builds; waf is enough for development only
#

import sys, os, subprocess
from waflib import Logs, Options, Errors

VERSION = '0.3.0'
APPNAME = 'iris'
top = '.'
out = 'build'

cflags_maintainer = ['-g', '-O0', '-Werror', '-Wall', '-Wshadow', '-Wcast-align',
                     '-Wno-uninitialized', '-Wformat-security', '-Winit-self']

glib_req_version = '2.16.0'
gtk_req_version = '3.0.0'


test_execution_order = \
  ["gdestructiblepointer-1", "gstamppointer-1",
   "free-list-1", "stack-1", "rrobin-1",
   "queue-1", "lf-queue-1", "ws-queue-1",
   "message-1", 
   "port-1", "arbiter-1", "receiver-1",
   "coordination-arbiter-1",
   "thread-1", "scheduler-1", "scheduler-manager-1", "gmainscheduler-1",
   "receiver-scheduler-1",
   "task-1", "service-1",
   "process-1",
   "scheduler-2",
   "progress-monitor-gtk-1", "progress-dialog-gtk-1"]


def options (opt):
	opt.load ('compiler_c')
	opt.load ('gnu_dirs')

	opt.add_option ('--enable-shared',
	                action = 'store_true',
	                dest = 'shared',
	                default = True,
	                help = 'Build shared version of library (default)')
	opt.add_option ('--disable-shared',
	                dest = 'shared',
	                action = 'store_false',
	                default = True,
	                help = 'Do not build shared version of library')

	opt.add_option ('--enable-static',
	                action = 'store_true',
	                dest = 'static',
	                default = False,
	                help = 'Build static version of library')
	opt.add_option ('--disable-static',
	                dest = 'static',
	                action = 'store_false',
	                default = False,
	                help = 'Do not build static version of library')

	opt.add_option ('--profile',
	                dest = 'profile',
	                action = 'store_true',
	                default = False,
	                help = 'Enable profiling')

def configure (conf):
	conf.load ('compiler_c')

	if not Options.options.shared and not Options.options.static:
		raise Errors.WafError("Error: both shared and static versions of library are disabled")

	# FIXME: could be win64 :) & why not just use G_OS_WIN32??
	if sys.platform == 'win32':
		conf.env.append_value ('DEFINES', 'WIN32')

	if Options.options.shared:
		conf.env['IRIS_SHARED'] = 'yes'
	if Options.options.static:
		conf.env['IRIS_STATIC'] = 'yes'

	#FIXME: make optional
	conf.env.append_value ('CFLAGS', cflags_maintainer)

	if Options.options.profile:
		conf.env.append_value ('CFLAGS', '-pg')
		conf.env.append_value ('LINKFLAGS', '-pg')

	conf.check_cfg (package = 'gobject-2.0',
	                uselib_store = 'GOBJECT',
	                atleast_version = glib_req_version,
	                args = '--cflags --libs',
	                mandatory = True)
	conf.check_cfg (package = 'gthread-2.0',
	                uselib_store = 'GTHREAD',
	                atleast_version = glib_req_version,
	                args = '--cflags --libs',
	                mandatory = True)
	conf.check_cfg (package = 'gio-2.0',
	                uselib_store = 'GIO',
	                atleast_version = glib_req_version,
	                args = '--cflags --libs',
	                mandatory = True)

	# FIXME: make optional, now libiris-gtk is split off
	conf.check_cfg (package = 'gtk+-3.0',
	                uselib_store = 'GTK',
	                atleast_version = gtk_req_version,
	                args = '--cflags --libs',
	                mandatory = True)

	conf.env['GTESTER'] = conf.find_program('gtester')

	conf.write_config_header ('config.h')

	print ""
	print " Iris ", VERSION
	print ""
	print "   Prefix...................: ", conf.env['PREFIX']

	print "   Library versions.........: ",
	if Options.options.shared: print "shared",
	if Options.options.static: print "static",
	print

	print "   Debug level..............:  not supported in waf"
	print "   Maintainer Compiler flags:  forced in waf"
	print "   Profiling................: ", Options.options.profile and 'yes' or 'no'
	print "   Build API reference......:  not supported in waf"
	print "   Enable GTK+ widgets......:  forced in waf"
	print ""
	print " Preview Bindings"
	print ""
	print "   GObject Introspection....:  not supported in waf"
	print "   Vala Bindings............:  not supported in waf"
	print "   Python Bindings..........:  not supported in waf"
	print ""


def build_library (bld, source='', target='', uselib='', includes=''):
	if bld.env['IRIS_SHARED']:
		bld (features = 'c cshlib',
		     source   = source,
		     target   = target,
		     uselib   = uselib,
		     includes = includes)

	if bld.env['IRIS_STATIC']:
		bld (features = 'c cstlib',
		     source   = source,
		     target   = target,
		     uselib   = uselib,
		     includes = includes)

def build (bld):
	# Build libraries
	#
	uselib = 'GOBJECT GTHREAD GIO GTK'

	src_dir = bld.path.find_dir ('iris')
	build_library (bld,
	               source = src_dir.ant_glob('*.c', src=True, dir=False),
	               target = 'iris',
	               uselib = uselib)
	               

	src_dir = bld.path.find_dir ('iris-gtk')
	build_library (bld,
	               source = src_dir.ant_glob('*.c', src=True, dir=False),
	               target = 'iris-gtk',
	               uselib = uselib,
	               includes = 'iris')


	# Build examples
	#
	examples_dir = bld.path.find_dir ('examples')
	for file in examples_dir.ant_glob('*.c', src=True, dir=False):
		example = bld (features = 'c cprogram',
		               source = file,
		               target = file.change_ext('').get_bld(),
		               uselib = uselib,
		               includes = '. iris iris-gtk',
		               use = 'iris iris-gtk',
		               install_path = None)

	# Build tests
	#
	tests_dir = bld.path.find_dir ('tests')
	for file in tests_dir.ant_glob('*.c', src=True, dir=False):
		test = bld (features = 'c cprogram',
		            source = file,
		            target = file.change_ext('').get_bld(),
		            uselib = uselib,
		            includes = '. iris iris-gtk tests',
		            use = 'iris iris-gtk',
		            install_path = None)

		# Set unit_test flag selectively so user can call:
		#   ./waf check --target=tests/foo-test
		# to just run one test
		if not Options.options.targets or (test.target.name in Options.options.targets.split(",")):
			test.unit_test = 1


def check (bld):
	# Make sure the tests are up to date
	build (bld)

	# Run 'check' after rebuild has actually taken place
	bld.add_post_fun (check_action)

def check_action (bld):
	# Run tests through gtester
	#
	test_nodes = []
	for test in test_execution_order:
		test_obj = bld.get_tgen_by_name (test)

		if not hasattr (test_obj,'unit_test') or not getattr(test_obj, 'unit_test'):
			continue

		file_node = test_obj.target.abspath()
		test_nodes.append (file_node)

	gtester_params = [bld.env['GTESTER']]
	gtester_params.append ('--verbose');

	# A little black magic to make the tests run
	gtester_env = os.environ
	gtester_env['LD_LIBRARY_PATH'] = gtester_env.get('LD_LIBRARY_PATH', '') + \
	                                   ':' + bld.path.get_bld().abspath()

	for test in test_nodes:
		gtester_params.append (test)

	if Logs.verbose > 1:
		print gtester_params
	print unicode (gtester_params)
	pp = subprocess.Popen (gtester_params,
	                       env = gtester_env)
	result = pp.wait ()

from waflib.Build import BuildContext
class check_context(BuildContext):
	cmd = 'check'
	fun = 'check'
