from setuptools import setup

setup(
	name = 'git-extensions',
	author = 'Peter Raboud',
	author_email = 'praboud@gmail.com',
	version = '0.1',
	description = 'Various git extensions implemented in python',
    scripts = ['bin/git-recent'],
    install_requires = ['pygit2', 'colorama'],
)
