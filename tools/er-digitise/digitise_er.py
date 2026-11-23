import sys, csv, json
import numpy as np
from PIL import Image

def load(p):
    return np.array(Image.open(p).convert('L'))<128

def refine_baseline(im, y0, x0, x1):
    best=None
    for y in range(y0-12,y0+13):
        c=im[y,x0:x1].sum()
        if best is None or c>best[0]: best=(c,y)
    return best[1]

def find_axis_x(im, by, x_guess, win=60):
    # left vertical axis: column with most dark pixels in band above baseline
    best=None
    for x in range(x_guess-win,x_guess+win):
        c=im[by-330:by-5,x].sum()
        if best is None or c>best[0]: best=(c,x)
    return best[1]

def ticks_x(im, by, ax, x1):
    cols=[]
    for x in range(ax-6,x1+10):
        if im[by+4:by+14,x].sum()>=6: cols.append(x)
    groups=[]
    for x in cols:
        if groups and x-groups[-1][-1]<=2: groups[-1].append(x)
        else: groups.append([x])
    return [float(np.mean(g)) for g in groups]

def ticks_y(im, by, ax):
    rows=[]
    for y in range(by-340,by+6):
        if im[y,ax-16:ax-3].sum()>=8: rows.append(y)
    groups=[]
    for y in rows:
        if groups and y-groups[-1][-1]<=2: groups[-1].append(y)
        else: groups.append([y])
    return [float(np.mean(g)) for g in groups]

def extract(im, by, ax, xs_cal, ys_cal):
    # xs_cal: (x_at_0ms, px_per_ms); ys_cal: (y_at_0dB, px_per_dB)
    x0,pxms=xs_cal; y0,pxdb=ys_cal
    xend=int(x0+520*pxms)
    h=np.zeros(xend-ax-3)
    for i,x in enumerate(range(ax+3,xend)):
        y=by-3; run=0; gap=0
        while y>by-340:
            if im[y,x]: run+=1+gap; gap=0
            else:
                gap+=1
                if gap>2: break
            y-=1
        h[i]=run
    cols=np.where(h>=8)[0]
    groups=[]
    for c in cols:
        if groups and c-groups[-1][-1]<=1: groups[-1].append(c)
        else: groups.append([c])
    taps=[]
    for g in groups:
        xs=np.array(g)+ax+3
        hh=h[g]
        # centre weighted by height
        xc=float((xs*hh).sum()/hh.sum())
        top=by-3-hh.max()
        gain=-(top-y0)/pxdb
        taps.append(dict(t=(xc-x0)/pxms,g=gain,w=len(g)))
    return taps
