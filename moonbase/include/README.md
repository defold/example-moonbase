# Bullet headers

These unmodified Bullet 3.25 headers are copied from the matching Defold engine
SDK's `ext/include`: BulletCollision, BulletDynamics, LinearMath and the two
common entry-point headers. They allow the extension to query the Bullet world
borrowed from `PhysicsEvent.m_World` without private Defold headers.

The engine provides the Bullet libraries. Keep these headers in sync with the
Bullet version used by that engine SDK. No Bullet implementation sources are
included. The headers retain their upstream notices and [zlib license](LICENSE.bullet.txt).
