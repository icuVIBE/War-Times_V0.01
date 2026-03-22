# ============================================================
# War Times .mesh Exporter for Blender 3.x / 4.x
# ============================================================
# Export selected meshes as .mesh binary files ready for D3D11.
#
# Install:  Edit > Preferences > Add-ons > Install > select this .py
#           Enable "Import-Export: War Times Mesh Exporter"
# Usage:    File > Export > War Times Mesh (.mesh)
#           Or select objects and export; only mesh objects are exported.
#
# Coordinate conversion (Blender → D3D11):
#   Blender: right-handed, Z-up, CCW front faces
#   D3D11:   left-handed,  Y-up, CW  front faces
#
#   Position:  (x, y, z) → (x, z, -y)
#   Normal:    (x, y, z) → (x, z, -y)
#   UV:        (u, v) → (u, 1-v)        (V flipped)
#   Winding:   reverse triangle indices  (CCW → CW)
# ============================================================

bl_info = {
    "name": "War Times Mesh Exporter",
    "author": "War Times Dev",
    "version": (1, 0, 0),
    "blender": (3, 0, 0),
    "location": "File > Export > War Times Mesh (.mesh)",
    "description": "Export meshes as .mesh binary for the War Times engine (D3D11)",
    "category": "Import-Export",
}

import bpy
import struct
import os
import bmesh
import shutil
from bpy_extras.io_utils import ExportHelper
from bpy.props import BoolProperty, FloatProperty, StringProperty
from mathutils import Matrix, Vector


def triangulate_mesh(mesh_data):
    """Triangulate a mesh using bmesh (non-destructive)."""
    bm = bmesh.new()
    bm.from_mesh(mesh_data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh_data)
    bm.free()


def _find_image_in_chain(node, visited=None):
    """Recursively walk upstream through linked nodes to find an Image Texture."""
    if visited is None:
        visited = set()
    if node in visited:
        return None
    visited.add(node)
    if node.type == 'TEX_IMAGE' and node.image:
        return node.image
    # Walk upstream through all input links
    for inp in node.inputs:
        for link in inp.links:
            result = _find_image_in_chain(link.from_node, visited)
            if result:
                return result
    return None


def find_base_color_image(obj):
    """Walk the object's materials to find the first base color image texture.
    Returns (bpy.types.Image or None, str reason)."""
    if not obj.data.materials:
        return None, "object has no materials"
    has_material = False
    for mat in obj.data.materials:
        if mat is None:
            continue
        has_material = True
        if not mat.use_nodes:
            continue
        tree = mat.node_tree
        # Find Principled BSDF or Diffuse BSDF and walk upstream from Base Color / Color
        for node in tree.nodes:
            if node.type == 'BSDF_PRINCIPLED':
                bc_input = node.inputs.get('Base Color')
                if bc_input and bc_input.is_linked:
                    img = _find_image_in_chain(bc_input.links[0].from_node)
                    if img:
                        return img, ""
            elif node.type == 'BSDF_DIFFUSE':
                c_input = node.inputs.get('Color')
                if c_input and c_input.is_linked:
                    img = _find_image_in_chain(c_input.links[0].from_node)
                    if img:
                        return img, ""
        # Fallback: find any TEX_IMAGE node in the tree
        for node in tree.nodes:
            if node.type == 'TEX_IMAGE' and node.image:
                return node.image, ""
        # Check if there are TEX_IMAGE nodes but with no image loaded
        for node in tree.nodes:
            if node.type == 'TEX_IMAGE' and node.image is None:
                return None, f"material '{mat.name}' has an Image Texture node but no image is loaded in it"
    if not has_material:
        return None, "all material slots are empty"
    return None, "materials have no Image Texture node — add one in the Shader Editor and load a texture"


def export_texture_for_object(obj, mesh_filepath, tex_dir):
    """Export the base color texture for an object.
    - mesh_filepath: the .mesh output path (used to derive the texture name)
    - tex_dir: root textures/ directory in the project
    Returns (exported: bool, message: str)."""
    image, reason = find_base_color_image(obj)
    if image is None:
        return False, f"No texture exported: {reason}"

    # Derive texture path that matches engine convention:
    # mesh at models/Gun/Rifle1.mesh → texture at textures/Gun/Rifle1.png
    mesh_name = os.path.splitext(os.path.basename(mesh_filepath))[0]

    # Try to figure out the subfolder from the mesh path
    # e.g. if mesh_filepath is .../models/Gun/Rifle1.mesh, subfolder = "Gun"
    parent = os.path.basename(os.path.dirname(mesh_filepath))
    if parent and parent.lower() != 'models':
        tex_subdir = os.path.join(tex_dir, parent)
        tex_filename = mesh_name + ".png"
    else:
        tex_subdir = tex_dir
        tex_filename = mesh_name + ".png"

    os.makedirs(tex_subdir, exist_ok=True)
    tex_path = os.path.join(tex_subdir, tex_filename)

    # If the image is packed or generated, save it out directly
    if image.packed_file or image.source == 'GENERATED':
        # Save using Blender's image.save_render or pack then save
        old_path = image.filepath_raw
        old_format = image.file_format
        image.filepath_raw = tex_path
        image.file_format = 'PNG'
        try:
            image.save()
        finally:
            image.filepath_raw = old_path
            image.file_format = old_format
        return True, f"Saved packed/generated texture → {tex_path}"

    # External image — copy the file if it exists
    src_path = bpy.path.abspath(image.filepath)
    if src_path and os.path.isfile(src_path):
        ext = os.path.splitext(src_path)[1].lower()
        if ext == '.png':
            # Direct copy
            shutil.copy2(src_path, tex_path)
            return True, f"Copied PNG → {tex_path}"
        elif ext in ('.bmp', '.jpg', '.jpeg', '.tga', '.tiff', '.exr', '.hdr'):
            # Re-save as PNG via Blender
            old_path = image.filepath_raw
            old_format = image.file_format
            image.filepath_raw = tex_path
            image.file_format = 'PNG'
            try:
                image.save()
            finally:
                image.filepath_raw = old_path
                image.file_format = old_format
            return True, f"Converted {ext} → {tex_path}"
        else:
            return False, f"Unsupported source format: {ext}"
    else:
        return False, f"Source image not found: {src_path}"


def _get_color_ramp_dominant(node):
    """Get the dominant color from a Color Ramp node.
    Uses the brightest stop (most likely to represent the material's intended color)."""
    if node.type != 'VALTORGB':  # Color Ramp internal type
        return None
    ramp = node.color_ramp
    if not ramp.elements:
        return None
    # Pick the element with the highest value (brightness)
    # For toon/cel shading, the brightest stop is usually the material's base color
    best = None
    best_brightness = -1
    for elem in ramp.elements:
        c = elem.color
        brightness = c[0] * 0.299 + c[1] * 0.587 + c[2] * 0.114
        if brightness > best_brightness:
            best_brightness = brightness
            best = (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
    return best


def _walk_to_color(node, visited=None):
    """Walk upstream from a node to extract a meaningful color value.
    Handles Color Ramp, RGB, Mix, and other intermediate nodes."""
    if visited is None:
        visited = set()
    if node in visited:
        return None
    visited.add(node)

    # Color Ramp → pick dominant color
    if node.type == 'VALTORGB':
        return _get_color_ramp_dominant(node)

    # RGB constant node
    if node.type == 'RGB':
        c = node.outputs[0].default_value
        return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)

    # Image texture → can't extract a single color, skip
    if node.type == 'TEX_IMAGE':
        return None

    # Shader to RGB → walk its input (the BSDF feeding it)
    if node.type == 'SHADERTORGB':
        shader_in = node.inputs.get('Shader')
        if shader_in and shader_in.is_linked:
            return _walk_to_color(shader_in.links[0].from_node, visited)

    # Mix RGB / Mix node → try the second color input (Factor-weighted)
    if node.type in ('MIX_RGB', 'MIX'):
        for name in ('Color2', 'B', 'Color1', 'A'):
            inp = node.inputs.get(name)
            if inp:
                if inp.is_linked:
                    result = _walk_to_color(inp.links[0].from_node, visited)
                    if result:
                        return result
                elif hasattr(inp, 'default_value') and len(inp.default_value) >= 3:
                    c = inp.default_value
                    return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)

    # Generic: try all color/vector inputs upstream
    for inp in node.inputs:
        if inp.is_linked:
            result = _walk_to_color(inp.links[0].from_node, visited)
            if result:
                return result

    return None


def get_material_base_color(mat):
    """Extract the base/diffuse color from a material as (R, G, B, A).
    Handles complex node chains: Diffuse→ShaderToRGB→ColorRamp→Emission, etc."""
    if mat is None:
        return None
    if not mat.use_nodes:
        dc = mat.diffuse_color
        return (dc[0], dc[1], dc[2], dc[3] if len(dc) > 3 else 1.0)

    tree = mat.node_tree

    # Strategy 1: Find the Material Output → walk upstream from Surface input
    for node in tree.nodes:
        if node.type == 'OUTPUT_MATERIAL' and node.is_active_output:
            surf = node.inputs.get('Surface')
            if surf and surf.is_linked:
                from_node = surf.links[0].from_node
                # If connected to Emission → walk its Color input upstream
                if from_node.type == 'EMISSION':
                    ci = from_node.inputs.get('Color')
                    if ci and ci.is_linked:
                        result = _walk_to_color(ci.links[0].from_node)
                        if result:
                            return result
                    elif ci and not ci.is_linked:
                        c = ci.default_value
                        return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
                # Principled BSDF
                elif from_node.type == 'BSDF_PRINCIPLED':
                    bc = from_node.inputs.get('Base Color')
                    if bc and bc.is_linked:
                        result = _walk_to_color(bc.links[0].from_node)
                        if result:
                            return result
                    elif bc and not bc.is_linked:
                        c = bc.default_value
                        return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
                # Diffuse BSDF
                elif from_node.type == 'BSDF_DIFFUSE':
                    ci = from_node.inputs.get('Color')
                    if ci and not ci.is_linked:
                        c = ci.default_value
                        return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
                # Try generic walk
                result = _walk_to_color(from_node)
                if result:
                    return result

    # Strategy 2: Find any Color Ramp and extract from it
    for node in tree.nodes:
        if node.type == 'VALTORGB':
            result = _get_color_ramp_dominant(node)
            if result:
                return result

    # Strategy 3: Find any BSDF and use its direct color
    for node in tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            bc = node.inputs.get('Base Color')
            if bc and not bc.is_linked:
                c = bc.default_value
                return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
        elif node.type == 'BSDF_DIFFUSE':
            ci = node.inputs.get('Color')
            if ci and not ci.is_linked:
                c = ci.default_value
                return (c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)

    # Last resort: viewport display color
    dc = mat.diffuse_color
    return (dc[0], dc[1], dc[2], dc[3] if len(dc) > 3 else 1.0)


def export_mesh_object(context, obj, filepath, apply_modifiers, default_color,
                       bake_material_colors=False):
    """Export a single mesh object to a .mesh binary file."""

    # Get evaluated mesh (with modifiers applied if requested)
    if apply_modifiers:
        depsgraph = context.evaluated_depsgraph_get()
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()
    else:
        mesh = obj.to_mesh()

    if mesh is None:
        return False, "Could not get mesh data"

    # Triangulate
    triangulate_mesh(mesh)

    # Ensure we have loop normals
    mesh.calc_loop_triangles()
    if hasattr(mesh, 'calc_normals_split'):
        mesh.calc_normals_split()

    # Pre-compute per-material-index colors if baking
    mat_colors = {}
    if bake_material_colors and obj.data.materials:
        for i, mat in enumerate(obj.data.materials):
            c = get_material_base_color(mat)
            if c:
                mat_colors[i] = c

    # Get vertex color layer (if any)
    color_layer = None
    if not bake_material_colors:  # vertex colors take precedence only when not baking
        if mesh.color_attributes:
            color_layer = mesh.color_attributes.active_color
        elif mesh.vertex_colors:
            color_layer = mesh.vertex_colors.active

    # Get UV layer (if any)
    uv_layer = None
    if mesh.uv_layers:
        uv_layer = mesh.uv_layers.active

    # Get object transform matrix and its normal matrix (inverse transpose of 3x3)
    obj_matrix = obj.matrix_world
    # Normal matrix = inverse transpose of the upper-left 3x3
    norm_matrix = obj_matrix.to_3x3().inverted_safe().transposed()
    # Check if the transform flips winding (negative determinant = odd number of negative scales)
    flip_winding = obj_matrix.determinant() < 0

    # Build vertex data by iterating loop triangles
    # Each unique (position, normal, color, uv) combination becomes a vertex
    vertex_map = {}   # (pos_tuple, norm_tuple, color_tuple, uv_tuple) → index
    vertices = []     # list of (pos, norm, color, uv) tuples
    indices = []      # triangle indices

    for tri in mesh.loop_triangles:
        tri_indices = []
        # Get color from material if baking
        if bake_material_colors:
            mat_color = mat_colors.get(tri.material_index, default_color)
        else:
            mat_color = None

        for loop_index in tri.loops:
            loop = mesh.loops[loop_index]
            vert = mesh.vertices[loop.vertex_index]

            # Apply object transform to position, THEN convert coordinates
            # Blender (x,y,z) → world space → D3D11 (x, z, -y)
            world_pos = obj_matrix @ vert.co
            px, py, pz = world_pos.x, world_pos.z, -world_pos.y

            # Apply object normal matrix, normalize, then convert coordinates
            if tri.use_smooth:
                raw_norm = loop.normal  # per-loop split normal
            else:
                raw_norm = tri.normal   # flat face normal
            world_norm = (norm_matrix @ Vector(raw_norm)).normalized()
            nx, ny, nz = world_norm.x, world_norm.z, -world_norm.y

            # Vertex color: baked material color > vertex color layer > default
            if mat_color is not None:
                cr, cg, cb, ca = mat_color
            elif color_layer is not None:
                if hasattr(color_layer, 'data') and len(color_layer.data) > loop_index:
                    c = color_layer.data[loop_index].color
                    cr, cg, cb, ca = c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0
                else:
                    cr, cg, cb, ca = default_color
            else:
                cr, cg, cb, ca = default_color

            # UV: flip V axis
            if uv_layer is not None:
                uv = uv_layer.data[loop_index].uv
                u, v = uv.x, 1.0 - uv.y
            else:
                u, v = 0.0, 0.0

            # Create unique vertex key (quantized for floating point matching)
            def q(f): return round(f, 6)
            key = (q(px), q(py), q(pz),
                   q(nx), q(ny), q(nz),
                   q(cr), q(cg), q(cb), q(ca),
                   q(u), q(v))

            if key in vertex_map:
                tri_indices.append(vertex_map[key])
            else:
                idx = len(vertices)
                vertex_map[key] = idx
                vertices.append((px, py, pz, nx, ny, nz, cr, cg, cb, ca, u, v))
                tri_indices.append(idx)

        # Reverse winding order: CCW (Blender) → CW (D3D11)
        # If object transform has negative determinant (mirrored), don't reverse (double-negative)
        if flip_winding:
            indices.extend([tri_indices[0], tri_indices[1], tri_indices[2]])
        else:
            indices.extend([tri_indices[0], tri_indices[2], tri_indices[1]])

    # Clean up temporary mesh
    if apply_modifiers:
        eval_obj.to_mesh_clear()
    else:
        obj.to_mesh_clear()

    if len(vertices) == 0 or len(indices) == 0:
        return False, "Mesh has no geometry"

    # ---- Write binary file ----
    vertex_count = len(vertices)
    index_count = len(indices)

    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    with open(filepath, 'wb') as f:
        # Header: magic, version, vertex count, index count
        f.write(struct.pack('<I', 0x4853454D))   # "MESH"
        f.write(struct.pack('<I', 1))             # version
        f.write(struct.pack('<I', vertex_count))
        f.write(struct.pack('<I', index_count))

        # Vertices: pos(3f) + normal(3f) + color(4f) + uv(2f) = 12 floats = 48 bytes
        for v in vertices:
            f.write(struct.pack('<12f', *v))

        # Indices: uint32
        for i in indices:
            f.write(struct.pack('<I', i))

    return True, f"Exported {vertex_count} verts, {index_count // 3} tris"


class WARTIMES_OT_export_mesh(bpy.types.Operator, ExportHelper):
    """Export selected objects as War Times .mesh binary files"""
    bl_idname = "export_mesh.wartimes"
    bl_label = "Export War Times Mesh"
    bl_options = {'PRESET'}

    filename_ext = ".mesh"

    filter_glob: StringProperty(
        default="*.mesh",
        options={'HIDDEN'},
    )

    apply_modifiers: BoolProperty(
        name="Apply Modifiers",
        description="Apply all modifiers before exporting",
        default=True,
    )

    export_selected: BoolProperty(
        name="Selected Only",
        description="Export only selected mesh objects (otherwise all visible meshes)",
        default=True,
    )

    batch_export: BoolProperty(
        name="Batch Export",
        description="Export each object as a separate .mesh file (filename = object name)",
        default=False,
    )

    bake_material_colors: BoolProperty(
        name="Bake Material Colors",
        description="Bake each material's base/diffuse color into vertex colors. "
                    "Ideal for models with multiple solid-color materials",
        default=True,
    )

    export_textures: BoolProperty(
        name="Export Textures",
        description="Export base color textures as PNG alongside the mesh",
        default=True,
    )

    textures_dir: StringProperty(
        name="Textures Directory",
        description="Root textures folder (engine textures/ directory). Leave blank to place textures next to meshes",
        default="",
        subtype='DIR_PATH',
    )

    default_color_r: FloatProperty(name="Default R", default=0.8, min=0.0, max=1.0)
    default_color_g: FloatProperty(name="Default G", default=0.8, min=0.0, max=1.0)
    default_color_b: FloatProperty(name="Default B", default=0.8, min=0.0, max=1.0)
    default_color_a: FloatProperty(name="Default A", default=1.0, min=0.0, max=1.0)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "apply_modifiers")
        layout.prop(self, "export_selected")
        layout.prop(self, "batch_export")
        layout.separator()
        layout.prop(self, "bake_material_colors")
        layout.separator()
        layout.prop(self, "export_textures")
        if self.export_textures:
            layout.prop(self, "textures_dir")
        layout.separator()
        layout.label(text="Default Vertex Color (when no color attribute):")
        row = layout.row()
        row.prop(self, "default_color_r", text="R")
        row.prop(self, "default_color_g", text="G")
        row.prop(self, "default_color_b", text="B")
        row.prop(self, "default_color_a", text="A")

    def execute(self, context):
        default_color = (self.default_color_r, self.default_color_g,
                         self.default_color_b, self.default_color_a)

        # Gather mesh objects
        if self.export_selected:
            objects = [o for o in context.selected_objects if o.type == 'MESH']
        else:
            objects = [o for o in context.view_layer.objects if o.type == 'MESH' and o.visible_get()]

        if not objects:
            self.report({'WARNING'}, "No mesh objects to export")
            return {'CANCELLED'}

        tex_dir = bpy.path.abspath(self.textures_dir) if self.textures_dir else ""

        if self.batch_export:
            # Export each object as separate file
            base_dir = os.path.dirname(self.filepath)
            exported = 0
            for obj in objects:
                # Sanitize name for filename
                safe_name = obj.name.replace(' ', '_')
                filepath = os.path.join(base_dir, safe_name + ".mesh")
                ok, msg = export_mesh_object(context, obj, filepath,
                                             self.apply_modifiers, default_color,
                                             self.bake_material_colors)
                if ok:
                    exported += 1
                    self.report({'INFO'}, f"{safe_name}: {msg}")
                    # Export texture
                    if self.export_textures:
                        dest = tex_dir if tex_dir else base_dir
                        tok, tmsg = export_texture_for_object(obj, filepath, dest)
                        if tok:
                            self.report({'INFO'}, f"{safe_name} texture: {tmsg}")
                        else:
                            self.report({'WARNING'}, f"{safe_name} texture: {tmsg}")
                else:
                    self.report({'WARNING'}, f"{safe_name}: {msg}")

            self.report({'INFO'}, f"Batch exported {exported}/{len(objects)} meshes")
        else:
            # Export first selected (or all joined as one — just export the active/first)
            if len(objects) == 1:
                obj = objects[0]
            elif context.active_object and context.active_object.type == 'MESH':
                obj = context.active_object
            else:
                obj = objects[0]

            ok, msg = export_mesh_object(context, obj, self.filepath,
                                         self.apply_modifiers, default_color,
                                         self.bake_material_colors)
            if ok:
                self.report({'INFO'}, msg)
                # Export texture
                if self.export_textures:
                    dest = tex_dir if tex_dir else os.path.dirname(self.filepath)
                    tok, tmsg = export_texture_for_object(obj, self.filepath, dest)
                    if tok:
                        self.report({'INFO'}, f"Texture: {tmsg}")
                    else:
                        self.report({'WARNING'}, f"Texture: {tmsg}")
            else:
                self.report({'ERROR'}, msg)
                return {'CANCELLED'}

        return {'FINISHED'}


def menu_func_export(self, context):
    self.layout.operator(WARTIMES_OT_export_mesh.bl_idname,
                         text="War Times Mesh (.mesh)")


def register():
    bpy.utils.register_class(WARTIMES_OT_export_mesh)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(WARTIMES_OT_export_mesh)


if __name__ == "__main__":
    register()
