setup_args = dict(
	name = 'git-extensions',
	author = 'Peter Raboud',
	author_email = 'praboud@gmail.com',
	version = '0.1',
	description = 'Various git extensions implemented in python',
    scripts = ['bin/git-recent'],
)
install_requires = ['pygit2']
try:
    from setuptools import setup
    setup_args['install_requires'] = install_requires
except ImportError:
    from distutils.core import setup
    setup_args['requires'] = install_requires

setup(**setup_args)
