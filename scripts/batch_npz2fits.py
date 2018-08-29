from epic import data_interface as DI
import argparse
import numpy as np

a = argparse.ArgumentParser(description='Convert batch of npz files to fits')
a.add_argument('files', metavar='files', type=str, nargs='*', default=[],
               help='*.npz files to convert to fits.')

for f in a.files:
    d = np.load(f)
    of = f[:-3] + 'fits'
    epic2fits(of, d['image'], np.ravel(d['hdr'])[0], d['image_nums'])
