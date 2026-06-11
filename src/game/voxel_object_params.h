//
// Created by A on 12.06.2026.
//

#ifndef VOXEL_OBJECT_PARAMS_H
#define VOXEL_OBJECT_PARAMS_H

struct voxel_object_params
{
    float cell_size;  // размер одной ячейки в мировых единицах
    float default_hp;
    float depth;
    float paralax;
    glm::vec2 origin;
};

#endif //VOXEL_OBJECT_PARAMS_H
