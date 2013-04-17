from setuptools import setup, Extension
import os

libgit2_path = os.getenv("LIBGIT2")
if libgit2_path is None:
    if os.name == 'nt':
        program_files = os.getenv("ProgramFiles")
        libgit2_path = '%s\libgit2' % program_files
    else:
        libgit2_path = '/usr'

#libgit2_bin = os.path.join(libgit2_path, 'bin')
libgit2_include = os.path.join(libgit2_path, 'include')
libgit2_lib = os.getenv('LIBGIT2_LIB', os.path.join(libgit2_path, 'lib'))

print libgit2_lib
print libgit2_include

setup(
	name = 'git-extensions',
	author = 'Peter Raboud',
	author_email = 'praboud@gmail.com',
	version = '0.1',
	description = 'Various git extensions implemented in python',
    scripts = ['bin/git-recent'],
    install_requires = ['pygit2', 'colorama'],
    ext_modules=[
        Extension(
            '_gitext',
            sources=['src/gitext.c'],
            include_dirs=[libgit2_include],
            library_dirs=[libgit2_lib],
            libraries=['git2'],
        ),
    ],
)
