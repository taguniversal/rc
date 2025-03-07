import bpy

# Clear existing objects
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

# Create a cube
bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 0))

# Save as COLLADA (.dae)
bpy.ops.wm.collada_export(filepath="cube.dae")

print("✅ Cube generated and exported as cube.dae")
