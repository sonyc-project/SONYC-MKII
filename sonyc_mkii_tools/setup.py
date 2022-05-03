import setuptools

NAME = 'mkiiread'

setuptools.setup(
    name=NAME,
    version='0.0.1',
    description='Reading SONYC Mk-ii Messages.',
    long_description=open('README.md').read().strip(),
    long_description_content_type='text/markdown',
    author='Bea Steers',
    author_email='bea.steers@gmail.com',
    url='https://github.com/sonyc-project/sonyc_mkii_tools',
    packages=[NAME],  # setuptools.find_packages()
    # entry_points={'console_scripts': ['{name}={name}:main'.format(name=NAME)]},
    install_requires=['fire'],
    license='MIT License',
    keywords='multiprocessing process except raise exception handling '
             'proxy remote ops result yield')
