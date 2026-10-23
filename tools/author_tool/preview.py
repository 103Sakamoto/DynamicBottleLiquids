"""Matplotlib preview shared by the desktop app and headless visual checks."""
import numpy as np
from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection
from .core import points_world,axis_rotation,appearance_rings


def draw_preview(figure, model, cavity_block=None, target_block=None, result=None, appearance=None, preserve=False, isolate=False, axis='+Z', center_offset=(0,0)):
    camera=None
    if preserve and figure.axes:
        old=figure.axes[0]
        camera=(old.elev,old.azim,old.get_xlim(),old.get_ylim(),old.get_zlim())
    figure.clear()
    ax = figure.add_subplot(121, projection="3d")
    side = figure.add_subplot(122)
    all_points = []
    if model:
        for shape in model["meshes"]:
            if isolate and shape['block']!=cavity_block:continue
            points = points_world(shape); all_points.append(points)
            faces = np.array(shape["indices"]).reshape(-1, 3)
            faces = faces[::max(1, len(faces) // 8000)]
            triangles = points[faces]
            lines = np.concatenate((triangles[:, [0, 1]], triangles[:, [1, 2]], triangles[:, [2, 0]]))
            selected = shape["block"] == cavity_block
            target = shape["block"] == target_block
            color = "#257a49" if selected else ("#2186bd" if target else "#aab1b9")
            collection=Line3DCollection(lines, colors=color, linewidths=.35,alpha=.55 if selected or target else .12)
            collection.set_picker(5);collection.shape_block=shape['block'];ax.add_collection3d(collection)
        points = np.concatenate(all_points)
        center = (points.min(0) + points.max(0)) / 2
        radius = max(float(np.ptp(points, axis=0).max()) / 2, .1) * 1.1
        ax.set_xlim(center[0] - radius, center[0] + radius)
        ax.set_ylim(center[1] - radius, center[1] + radius)
        ax.set_zlim(center[2] - radius, center[2] + radius)
        ax.set_box_aspect((1, 1, 1))
    if result:
        rings = np.asarray(result["rings"]).copy()
        if appearance:
            try:rings=appearance_rings(rings,appearance['glass_thickness'])
            except ValueError:appearance=None
        inverse = np.linalg.inv(np.asarray(result["canonical_matrix"]))
        angles = np.linspace(0, 2 * np.pi, 65)
        for z, r in rings:
            ring = np.column_stack((r * np.cos(angles), r * np.sin(angles), np.full_like(angles, z)))
            world = ring @ inverse[:3, :3].T + inverse[:3, 3]
            ax.plot(*world.T, color="#d97706", linewidth=1)
        if appearance and appearance['liquid_level']>0 and rings[-1,0]>rings[0,0]:
            level=rings[0,0]+(rings[-1,0]-rings[0,0])*appearance['liquid_level']
            zs=np.r_[rings[rings[:,0]<level,0],level]
            rs=np.interp(zs,rings[:,0],rings[:,1]);angles=np.linspace(0,2*np.pi,33)
            contours=[np.column_stack((r*np.cos(angles),r*np.sin(angles),np.full_like(angles,z))) for z,r in zip(zs,rs)]
            faces=[]
            for first,second in zip(contours,contours[1:]):
                faces.extend([[first[j],first[j+1],second[j+1],second[j]] for j in range(32)])
            if contours:faces.append(contours[-1])
            transformed=[np.asarray(f)@inverse[:3,:3].T+inverse[:3,3] for f in faces]
            ax.add_collection3d(Poly3DCollection(transformed,facecolors=appearance['liquid_color'],alpha=appearance['opacity'],edgecolors='none'))
            side.fill_betweenx(zs,-rs,rs,color=appearance['liquid_color'],alpha=appearance['opacity'])
        origin=inverse[:3,3];length=max(rings[-1,0]*.15,1)
        for i,(label,color) in enumerate((('Center X','#b33434'),('Center Y','#267642'),('Up','#2456bd'))):
            vector=inverse[:3,i]*length;ax.quiver(*origin,*vector,color=color);ax.text(*(origin+vector),label,color=color,fontsize=7)
        side.plot(rings[:, 1], rings[:, 0], "o-", color="#d97706", markersize=3)
        side.plot(-rings[:, 1], rings[:, 0], "o-", color="#d97706", markersize=3)
        side.fill_betweenx(rings[:, 0], -rings[:, 1], rings[:, 1], alpha=.12, color="#d97706")
        side.set_aspect("equal", adjustable="datalim")
        side.set_title(f"Calculated cavity · {len(rings)} rings")
    else:
        side.text(.5, .5, "Select a shape, then Prepare bottle.\n\nGreen: selected liquid\nBlue: glass target\nOrange: calculated cavity",
                  transform=side.transAxes, ha="center", va="center", color="#59636e")
        side.set_title("Cavity cross-section")
        if model and target_block is not None:
            shape=next(s for s in model['meshes'] if s['block']==target_block);rotation=axis_rotation(axis)
            local=points_world(shape)@rotation.T;center=(local.min(0)+local.max(0))/2
            center[:2]+=center_offset;center[2]=local[:,2].min();origin=rotation.T@center;length=max(np.ptp(local[:,2])*.15,1)
            for i,(label,color) in enumerate((('Center X','#b33434'),('Center Y','#267642'),('Up','#2456bd'))):
                vector=rotation.T[:,i]*length;ax.quiver(*origin,*vector,color=color);ax.text(*(origin+vector),label,color=color,fontsize=7)
    ax.set_title("Preview · click a shape / drag to rotate")
    ax.set_xlabel("X"); ax.set_ylabel("Y"); ax.set_zlabel("Z")
    side.set_xlabel("Radius (model units)"); side.set_ylabel("Upright height")
    side.grid(alpha=.2)
    figure.tight_layout()
    if camera:
        elev,azim,x,y,z=camera;ax.view_init(elev=elev,azim=azim);ax.set_xlim(x);ax.set_ylim(y);ax.set_zlim(z)


def draw_cavity(figure,result=None,appearance=None):
    """Persistent 2D preview, independent of the advanced 3D window."""
    figure.clear();ax=figure.add_subplot(111)
    if result:
        try:rings=appearance_rings(result['rings'],appearance['glass_thickness'] if appearance else .16)
        except ValueError as error:
            ax.text(.5,.5,str(error),transform=ax.transAxes,ha='center',wrap=True,color='#B10E1C');figure.tight_layout();return
        ax.plot(rings[:,1],rings[:,0],'o-',color='#D77B00',markersize=3)
        ax.plot(-rings[:,1],rings[:,0],'o-',color='#D77B00',markersize=3)
        ax.fill_betweenx(rings[:,0],-rings[:,1],rings[:,1],color='#F8E9D6')
        if appearance:
            level=rings[0,0]+np.ptp(rings[:,0])*appearance['liquid_level']
            zs=np.r_[rings[rings[:,0]<level,0],level];rs=np.interp(zs,rings[:,0],rings[:,1])
            ax.fill_betweenx(zs,-rs,rs,color=appearance['liquid_color'],alpha=appearance['opacity'])
        ax.set_title(f"Calculated cavity · {len(rings)} rings");ax.set_aspect('equal',adjustable='box')
        ax.set_xlabel('Radius (mesh units)');ax.set_ylabel('Upright height');ax.grid(alpha=.15)
    else:
        ax.text(.5,.5,'Select a shape and prepare the bottle\n\nto see its cavity here.',transform=ax.transAxes,ha='center',va='center',color='#616161');ax.set_axis_off()
    figure.tight_layout()

