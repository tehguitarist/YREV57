import numpy as np, csv
from PIL import Image
def load(p): return np.array(Image.open(p).convert('L'))<128
def best_stretch(im,y0,xlo,xhi,win=45):
    best=None
    for y in range(y0-win,y0+win+1):
        xs=np.where(im[y,xlo:xhi])[0]+xlo
        if len(xs)<300: continue
        groups=[[xs[0],xs[0]]]
        for x in xs[1:]:
            if x-groups[-1][1]<=6: groups[-1][1]=x
            else: groups.append([x,x])
        g=max(groups,key=lambda g:g[1]-g[0])
        L=g[1]-g[0]
        if best is None or L>best[0]: best=(L,y,g[0],g[1])
    return best
guess={'p-23.png':[('S-HALL',806,1058),('L-HALL',1500,1749),('RANDOM',2184,2430),('REVERSE',2925,3225)],
       'p-24.png':[]}
for pg,lst in guess.items():
    im=load('/home/claude/rev5_om/er/'+pg)
    for name,yl,yr in lst:
        for lab,yg in (('L',yl),('R',yr)):
            for sz,(xlo,xhi) in (('4.0',(1350,2420)),('1.0',(200,1250))):
                print(pg,name,lab,sz,best_stretch(im,yg,xlo,xhi))
