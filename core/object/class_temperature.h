/**************************************************************************/
/*  class_temperature.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef CLASS_TEMPERATURE_H
#define CLASS_TEMPERATURE_H

#include <type_traits>

// Forward-declare all of the types.
class String;
class StringName;
class Variant;
class NodePath;
class RID;
class Callable;
class Signal;
class Object;
class Node;
class Node2D;
class Node3D;
class Viewport;

#ifndef X11_ENABLED
// Conflicts with the Window defined in platform
class Window;
#endif

class RenderingServer;
class PhysicsServer2D;
class PhysicsServer3D;
class AudioServer;
class DisplayServer;
class TextServer;
class XRServer;
class CameraServer;
class NavigationServer2D;
class NavigationServer3D;
class WorkerThreadPool;
class MessageQueue;
class OS;
class Engine;
class MainLoop;
class SceneTree;

// Default: cold
template <typename T>
struct IsHotClass : std::false_type {};

// Below is considered "hot"

// Core primitives
template <>
struct IsHotClass<String> : std::true_type {};
template <>
struct IsHotClass<StringName> : std::true_type {};
template <>
struct IsHotClass<Variant> : std::true_type {};
template <>
struct IsHotClass<NodePath> : std::true_type {};
template <>
struct IsHotClass<RID> : std::true_type {};
template <>
struct IsHotClass<Callable> : std::true_type {};
template <>
struct IsHotClass<Signal> : std::true_type {};

// Core object/node hierarchy
template <>
struct IsHotClass<Object> : std::true_type {};
template <>
struct IsHotClass<Node> : std::true_type {};
template <>
struct IsHotClass<Node2D> : std::true_type {};
template <>
struct IsHotClass<Node3D> : std::true_type {};
template <>
struct IsHotClass<Viewport> : std::true_type {};

#ifndef X11_ENABLED
// Conflicts with the Window defined in platform
template <>
struct IsHotClass<Window> : std::true_type {};
#endif

// All servers
template <>
struct IsHotClass<RenderingServer> : std::true_type {};
template <>
struct IsHotClass<PhysicsServer2D> : std::true_type {};
template <>
struct IsHotClass<PhysicsServer3D> : std::true_type {};
template <>
struct IsHotClass<AudioServer> : std::true_type {};
template <>
struct IsHotClass<DisplayServer> : std::true_type {};
template <>
struct IsHotClass<TextServer> : std::true_type {};
template <>
struct IsHotClass<XRServer> : std::true_type {};
template <>
struct IsHotClass<CameraServer> : std::true_type {};
template <>
struct IsHotClass<NavigationServer2D> : std::true_type {};
template <>
struct IsHotClass<NavigationServer3D> : std::true_type {};

// Core systems
template <>
struct IsHotClass<WorkerThreadPool> : std::true_type {};
template <>
struct IsHotClass<MessageQueue> : std::true_type {};
template <>
struct IsHotClass<OS> : std::true_type {};
template <>
struct IsHotClass<Engine> : std::true_type {};
template <>
struct IsHotClass<MainLoop> : std::true_type {};
template <>
struct IsHotClass<SceneTree> : std::true_type {};

#endif // CLASS_TEMPERATURE_H
