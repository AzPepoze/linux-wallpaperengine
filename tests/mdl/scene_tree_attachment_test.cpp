#include <math.h>
#include <stdio.h>

#include "wallpaper/2d/tree/scene_tree.h"

#include "../test_util.h"
using test::check;

void runSceneRotationTests();

void runSceneAttachmentTests() {
    SceneTree tree;
    SceneTreeNode parent;
    parent.id = 1;
    parent.origin = {10.0f, 20.0f, 0.0f};
    std::array<float, 16> socket = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 4, 0, 1};
    parent.attachment_transforms.emplace("socket", socket);
    tree.addNode(parent);

    SceneTreeNode child;
    child.id = 2;
    child.parent_id = 1;
    child.attachment = "socket";
    child.origin = {2.0f, 5.0f, 0.0f};
    tree.addNode(child);

    SceneTreeNode fallback;
    fallback.id = 3;
    fallback.parent_id = 1;
    fallback.attachment = "missing";
    fallback.origin = {2.0f, 5.0f, 0.0f};
    tree.addNode(fallback);
    tree.rebuildHierarchy();

    float position[3] = {};
    check(tree.worldPosition(2, position), "attached node world position resolves");
    check(fabsf(position[0] - 9.0f) < 1e-4f && fabsf(position[1] - 29.0f) < 1e-4f,
          "attachment transform is applied before child local transform");
    check(tree.worldPosition(3, position), "missing attachment world position resolves");
    check(fabsf(position[0] - 12.0f) < 1e-4f && fabsf(position[1] - 25.0f) < 1e-4f,
          "unknown attachment falls back to the parent transform");

    // Rotation makes attachment/child multiplication order observable.
    socket[0] = socket[5] = 0.0f;
    socket[1] = 1.0f;
    socket[4] = -1.0f;
    parent.attachment_transforms["socket"] = socket;
    tree.addNode(parent);
    check(tree.worldPosition(2, position) && fabsf(position[0] - 2.0f) < 1e-4f && fabsf(position[1] - 26.0f) < 1e-4f,
          "attachment rotation transforms child translation before parent translation");

    runSceneRotationTests();
}

void runSceneRotationTests() {
    SceneTree tree;
    SceneTreeNode root;
    root.id = 10;
    root.origin = {0.0f, 0.0f, 0.0f};
    root.angles = {0.0f, 0.0f, 90.0f};
    tree.addNode(root);

    SceneTreeNode child;
    child.id = 11;
    child.parent_id = 10;
    child.origin = {1.0f, 0.0f, 0.0f};
    tree.addNode(child);
    tree.rebuildHierarchy();

    float position[3] = {};
    check(tree.worldPosition(11, position), "rotated node world position resolves");
    check(fabsf(position[0]) < 1e-4f && fabsf(position[1] - 1.0f) < 1e-4f,
          "scene node angles are degrees: 90 rotates (1,0) to (0,1)");
}

