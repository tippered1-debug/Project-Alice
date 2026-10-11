"""Shared geography helpers: lon/lat -> game map pixels through the fitted warp."""
import numpy as np
from scipy.interpolate import RBFInterpolator
from scipy.ndimage import map_coordinates
W,H=5616,2160
def miller(lat): return 1.25*np.log(np.tan(np.pi/4+0.4*np.radians(np.clip(lat,-80,89.5))))
MT,MB=miller(88.0),miller(-52.0)
def feat(lon,lat):
    return ((np.asarray(lon)+170.0)%360)/360*W, (MT-miller(np.asarray(lat)))/(MT-MB)*H
_grid=None
STEP=0.25
def _build():
    c=np.load('warp_ctrl.npz'); src,dst=c['src'],c['dst']
    fx,fy=feat(src[:,0],src[:,1]); F=np.stack([fx,fy],1)
    rbf=RBFInterpolator(F,dst-F,kernel='thin_plate_spline',smoothing=len(F)*40.0,neighbors=60)
    lons=np.arange(-170,190+STEP,STEP); lats=np.arange(89,-60-STEP,-STEP)
    LO,LA=np.meshgrid(lons,lats)
    gx,gy=feat(LO.ravel()-0.0,LA.ravel())
    # keep x monotonic across the grid (no modulo inside the grid)
    gx=(LO.ravel()+170.0)/360*W
    d=rbf(np.stack([gx,gy],1))
    return lons,lats,d[:,0].reshape(LO.shape),d[:,1].reshape(LO.shape)
def to_pixel(lon,lat):
    global _grid
    if _grid is None:
        try:
            z=np.load('warp_grid.npz'); _grid=(z['lons'],z['lats'],z['dx'],z['dy'])
        except FileNotFoundError:
            _grid=_build(); np.savez('warp_grid.npz',lons=_grid[0],lats=_grid[1],dx=_grid[2],dy=_grid[3])
    lons,lats,dx,dy=_grid
    lon=np.asarray(lon,dtype=float); lat=np.asarray(lat,dtype=float)
    lonu=np.where(lon<-170,lon+360,lon)   # unwrapped: -170..190
    ci=(lonu-lons[0])/STEP; ri=(lats[0]-np.clip(lat,-60,89))/STEP
    ddx=map_coordinates(dx,[ri,ci],order=1,mode='nearest'); ddy=map_coordinates(dy,[ri,ci],order=1,mode='nearest')
    x=(lonu+170.0)/360*W+ddx; y=(MT-miller(lat))/(MT-MB)*H+ddy
    return x,y   # x may fall slightly outside [0,W); caller wraps
