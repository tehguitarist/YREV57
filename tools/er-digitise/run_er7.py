import numpy as np, csv, sys
sys.path.insert(0,'.')
from digitise_er import *
pages={'p-23.png':{'modes':[1,2,3],'L':[1009,1942,2883],'R':[1371,2308,3233]},
       'p-24.png':{'modes':[4,5,6],'L':[1004,1944,2869],'R':[1366,2301,3222]}}
names={1:'SMALL HALL',2:'LARGE HALL',3:'RANDOM',4:'REVERSE',5:'PLATE',6:'SPRING'}
rows=[]; cal=[]
for pg,d in pages.items():
    im=load('/home/claude/rev7_work/er/'+pg)
    for k,m in enumerate(d['modes']):
        for size,xg in ((1.0,265),(4.0,1385)):
            for ch in 'LR':
                by=refine_baseline(im,d[ch][k],xg,xg+800)
                ax=find_axis_x(im,by,xg)
                tx=ticks_x(im,by,ax,ax+830)
                ty=ticks_y(im,by,ax)
                # expect 6 x ticks 0..500 and 5 y ticks 0..-40
                if len(tx)>=6:
                    xs=np.polyfit(np.arange(len(tx[:6]))*100,tx[:6],1)
                    pxms,x0=xs[0],xs[1]
                else: pxms,x0=None,None
                if len(ty)>=5:
                    ys=np.polyfit(np.arange(5)*-10,ty[:5],1)
                    pxdb=-ys[0]; y0=ys[1]   # y = y0 - pxdb*dB? dB negative => y increases
                    pxdb=abs(ys[0]); y0=ys[1]
                else: pxdb,y0=None,None
                cal.append((pg,m,size,ch,by,ax,len(tx),len(ty),pxms,x0,pxdb,y0))
                if pxms and pxdb:
                    taps=extract(im,by,ax,(x0,pxms),(y0,pxdb))
                    for i,t in enumerate(taps):
                        rows.append((m,names[m],size,ch,i+1,round(t['t'],1),round(t['g'],1),t['w']))
for c in cal: print(c)
with open('rev7_er_taps_raw.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['mode','mode_name','room_size','channel','tap','time_ms','gain_db','pixel_width']); w.writerows(rows)
print(len(rows))
