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

    // A node's local matrix splits back into the origin, scale and angles it was built from.
    SceneTreeNode source;
    source.id = 20;
    source.origin = {3.0f, -4.0f, 5.0f};
    source.scale = {2.0f, 0.5f, 3.0f};
    source.angles = {20.0f, -35.0f, 70.0f};
    tree.addNode(source);
    mat4x4 local;
    check(tree.localTransform(20, local), "local transform resolves");
    SceneTreeNode rebuilt;
    SceneTree::decompose(local, rebuilt);
    bool same = true;
    for (size_t i = 0; i < 3; ++i) {
        same = same && fabsf(rebuilt.origin[i] - source.origin[i]) < 1e-3f;
        same = same && fabsf(rebuilt.scale[i] - source.scale[i]) < 1e-3f;
        same = same && fabsf(rebuilt.angles[i] - source.angles[i]) < 1e-2f;
    }
    check(same, "decompose recovers origin, scale and angles");

    // Rotating around the node's own axes: 90 degrees about z, then 90 about local x.
    const float z90[3] = {0.0f, 0.0f, 90.0f};
    const float x90[3] = {90.0f, 0.0f, 0.0f};
    mat4x4 base, extra, combined;
    SceneTree::rotationFromAngles(z90, base);
    SceneTree::rotationFromAngles(x90, extra);
    mat4x4_mul(combined, base, extra);
    float result[3];
    SceneTree::anglesFromRotation(combined, result);
    mat4x4 again;
    SceneTree::rotationFromAngles(result, again);
    bool equal = true;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row) equal = equal && fabsf(again[column][row] - combined[column][row]) < 1e-4f;
    check(equal, "angles taken from a combined rotation rebuild the same rotation");

    // A hidden group hides what sits below it; removing a node frees its children.
    SceneTreeNode group;
    group.id = 30;
    group.visible = false;
    SceneTreeNode under;
    under.id = 31;
    under.parent_id = 30;
    tree.addNode(group);
    tree.addNode(under);
    tree.rebuildHierarchy();
    check(!tree.ancestorsVisible(31) && tree.ancestorsVisible(30), "a hidden group hides its children");
    tree.removeNode(30);
    check(tree.find(30) == nullptr && tree.find(31) && tree.find(31)->parent_id == 0,
          "removing a node turns its children into roots");
}

