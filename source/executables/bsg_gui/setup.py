from distutils.core import setup

setup(
    name='BSG_GUI',
    version='1.0',
    author='L. Hayen',
    author_email='leendert.hayen@gmail.com',
    packages=['bsg_gui', 'bsg_gui.utils', 'bsg_gui.ui'],
    license='LICENSE.txt',
    description='Graphical user interface for the Beta Spectrum Generator library.',
    long_description='',
    install_requires=[
        "shell (>= 1.0.1)",
        "QDarkStyle (>= 2.6.4)",
    	"PySide (>= 1.2.4)",
    	"configparser (>= 3.5.0)",
    	"numpy"
        ],
)
