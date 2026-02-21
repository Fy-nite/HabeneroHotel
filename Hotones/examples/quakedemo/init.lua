-- QuakeDemoProject / init.lua
-- A Quake-inspired FPS demo for the HoTones engine.
--
-- Controls:
--   WASD        = Move          LSHIFT  = Run
--   MOUSE       = Look          SPACE   = Jump
--   LMB         = Fire          RMB     = Alt-fire (future)
--   1-5 / WHEEL = Select weapon
--   R           = Suicide respawn

------------------------------------------------------------------------
-- Weapon constants
------------------------------------------------------------------------

local WPN_AXE = 1   -- melee
local WPN_SG  = 2   -- Shotgun
local WPN_SSG = 3   -- Super Shotgun
local WPN_NG  = 4   -- Nailgun
local WPN_RL  = 5   -- Rocket Launcher

local WEAPON_NAMES    = { "AXE", "SHOTGUN", "SUPER SHOTGUN", "NAILGUN", "ROCKET LAUNCHER" }
local WEAPON_AMMO     = { nil, "shells", "shells", "nails", "rockets" }
local WEAPON_COST     = { 0, 1, 2, 1, 1 }
local WEAPON_COOLDOWN = { 0.50, 0.50, 0.85, 0.10, 0.85 }
local WEAPON_DAMAGE   = { 30, 50, 95, 15, 120 }

------------------------------------------------------------------------
-- Enemy definitions
------------------------------------------------------------------------

local ENEMY_DEFS = {
    grunt   = { hp = 60,  color = { 110, 85,  55  }, scale = 0.90, melee = 8  },
    soldier = { hp = 90,  color = { 55,  130, 55  }, scale = 1.00, melee = 12 },
    knight  = { hp = 160, color = { 80,  80,  165 }, scale = 1.10, melee = 18 },
    ogre    = { hp = 210, color = { 165, 68,  38  }, scale = 1.30, melee = 24 },
}

local SPAWN_POINTS = {
    { x =  8,   z = -2,  type = "soldier" },
    { x = -8,   z = -2,  type = "soldier" },
    { x =  0,   z = -8,  type = "knight"  },
    { x =  5,   z = -14, type = "grunt"   },
    { x = -5,   z = -14, type = "grunt"   },
    { x =  0,   z = -18, type = "ogre"    },
    { x =  12,  z = -10, type = "soldier" },
    { x = -12,  z = -10, type = "soldier" },
    { x =  6,   z = -6,  type = "grunt"   },
    { x = -6,   z = -6,  type = "grunt"   },
}

------------------------------------------------------------------------
-- Game table
------------------------------------------------------------------------

Q = {}

-- Player state (tracked in Lua for weapon/projectile logic)
Q.px, Q.py, Q.pz = 0, 1.8, 6   -- updated each frame from player.getPos()
Q.yaw_rad   = 0  -- cached from player.getLook() each frame
Q.pitch_rad = 0  -- cached from player.getLook() each framewa
Q.velY  = 0
Q.onGround = true

Q.health = 100
Q.armor  = 50
Q.kills  = 0
Q.dead   = false
Q.deadTimer = 0

Q.ammo   = { shells = 25, nails = 30, rockets = 5 }
Q.weapon = WPN_SG
Q.fireCd = 0
Q.muzzleTimer = 0
Q.bobPhase    = 0

-- Collections
Q.projectiles = {}
Q.effects     = {}
Q.enemies     = {}

-- Screen flash (damage, etc.)
Q.flash = { r = 0, g = 0, b = 0, a = 0, t = 0 }

-- Light handles
Q.sunLight    = nil
Q.muzzleLight = nil
Q.torches     = {}   -- { handle, x, y, z, phase }

------------------------------------------------------------------------
-- Required Init table
------------------------------------------------------------------------

Init = {
    -- MainScene = "models/MainScene.gltf",
    MainClass = Q,
    Debug     = true,
}

------------------------------------------------------------------------
-- Internal helpers
------------------------------------------------------------------------

-- Forward direction vector from yaw_rad / pitch_rad (radians).
-- yaw_rad=0 → facing -Z; pitch_rad>0 → looking up.
-- Matches the values returned by player.getLook() directly.
local function fireDir(yaw_rad, pitch_rad)
    local cp = math.cos(pitch_rad)
    local sp = math.sin(pitch_rad)
    return math.sin(yaw_rad) * cp, sp, -math.cos(yaw_rad) * cp
end

local function dist3(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return math.sqrt(dx*dx + dy*dy + dz*dz)
end

local function dist2(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx*dx + dz*dz)
end

local function spawnEnemies()
    Q.enemies = {}
    for i, sp in ipairs(SPAWN_POINTS) do
        local def = ENEMY_DEFS[sp.type]
        Q.enemies[i] = {
            id          = i,
            x           = sp.x,
            y           = 0,
            z           = sp.z,
            type        = sp.type,
            hp          = def.hp,
            maxHp       = def.hp,
            scale       = def.scale,
            alive       = true,
            respawnCd   = 0,
            rot         = math.random() * 360,
            painTimer   = 0,
            atkPhase    = 0,   -- last integer attack-phase seen
        }
    end
end

------------------------------------------------------------------------
-- Q:Init
------------------------------------------------------------------------

function Q:Init()
    server.log("Quake Demo init!")

    -- Scene lighting: dark dungeon
    lighting.setAmbient(22, 16, 11, 0.22)

    self.sunLight = lighting.add(lighting.DIRECTIONAL, 0, 0, 0,
                                  175, 155, 125,  0.45)
    lighting.setDir(self.sunLight, -0.30, -1.0, -0.45)

    -- Muzzle flash (starts invisible)
    self.muzzleLight = lighting.add(lighting.POINT, 0, 0, 0,
                                     255, 200, 80,  0, 7)

    -- Torch flicker lights at each column
    local torchPos = {
        {  6, 2.2, -5.8 },  { -6, 2.2, -5.8 },
        {  6, 2.2, -15.2 }, { -6, 2.2, -15.2 },
    }
    for _, tp in ipairs(torchPos) do
        local h = lighting.add(lighting.POINT, tp[1], tp[2], tp[3],
                                255, 138, 38,  1.1, 9)
        table.insert(self.torches, {
            handle = h,
            phase  = math.random() * 6.28,
        })
    end

    spawnEnemies()
    server.log("Quake Demo ready — " .. #Q.enemies .. " enemies on map.")
end

------------------------------------------------------------------------
-- Q:Update
------------------------------------------------------------------------

function Q:Update()
    local dt = GetFrameTime()
    if dt <= 0 then dt = 0.016 end

    if self.dead then
        self.deadTimer = self.deadTimer + dt
        if self.deadTimer >= 3.0 then self:respawn() end
        return
    end

    self:updateMovement(dt)
    self:updateWeapons(dt)
    self:updateProjectiles(dt)
    self:updateEffects(dt)
    self:updateEnemies(dt)
    self:updateLighting(dt)
    self:updateFlash(dt)
end

------------------------------------------------------------------------
-- Movement + camera
------------------------------------------------------------------------

function Q:updateMovement(dt)
    -- ── Real look direction from C++ Player ───────────────────────────────
    -- player.getLook() returns (-lookRotation.x, -lookRotation.y) in radians
    -- so that yaw_rad=0 faces -Z and positive yaw turns right, positive pitch
    -- looks up — matching the fireDir() convention exactly.
    self.yaw_rad, self.pitch_rad = player.getLook()

    -- ── Real world position from C++ Player ───────────────────────────────
    -- player.getPos() returns the eye-space position after full collision /
    -- physics resolution.  Used as fire origin and for proximity checks.
    self.px, self.py, self.pz = player.getPos()

    -- ── Bob accumulator (visual only) ─────────────────────────────────────
    local moving = input.isKeyDown(input.KEY_W) or input.isKeyDown(input.KEY_S)
                or input.isKeyDown(input.KEY_A) or input.isKeyDown(input.KEY_D)
    if moving then self.bobPhase = self.bobPhase + dt * 9 end

    -- ── Suicide respawn key ────────────────────────────────────────────────
    if input.isKeyPressed(input.KEY_R) then
        self:takeDamage(9999, 200, 0, 0)
    end
end

------------------------------------------------------------------------
-- Weapon selection + firing
------------------------------------------------------------------------

function Q:updateWeapons(dt)
    self.fireCd      = math.max(0, self.fireCd      - dt)
    self.muzzleTimer = math.max(0, self.muzzleTimer - dt)

    -- Number keys 1–5
    local numKeys = { input.KEY_1, input.KEY_2, input.KEY_3,
                      input.KEY_4, input.KEY_5 }
    for i, k in ipairs(numKeys) do
        if input.isKeyPressed(k) then self.weapon = i end
    end

    -- Scroll wheel cycle
    local wheel = input.getMouseWheel()
    if wheel > 0 then self.weapon = math.max(1, self.weapon - 1) end
    if wheel < 0 then self.weapon = math.min(5, self.weapon + 1) end

    -- Fire: axe is press-only; others auto-fire while held
    local firing = input.isMouseDown(input.MOUSE_LEFT)
    if self.weapon == WPN_AXE then
        firing = input.isMousePressed(input.MOUSE_LEFT)
    end

    if firing and self.fireCd <= 0 then self:fire() end
end

------------------------------------------------------------------------
-- Fire
------------------------------------------------------------------------

function Q:fire()
    local atype = WEAPON_AMMO[self.weapon]
    local cost  = WEAPON_COST[self.weapon]
    if atype and (self.ammo[atype] or 0) < cost then
        server.log("Out of " .. (atype or "ammo") .. "!")
        return
    end
    if atype then self.ammo[atype] = self.ammo[atype] - cost end

    self.fireCd      = WEAPON_COOLDOWN[self.weapon]
    self.muzzleTimer = 0.12

    local ox, oy, oz = self.px, self.py, self.pz

    if self.weapon == WPN_AXE then
        -- Instant melee in front
        for _, e in ipairs(self.enemies) do
            if e.alive and dist3(ox, oy, oz, e.x, e.y + 1, e.z) < 2.5 then
                self:hitEnemy(e, WEAPON_DAMAGE[WPN_AXE])
                table.insert(self.effects, {
                    x = e.x, y = e.y + 1.2, z = e.z, t = 0.25, type = "spark"
                })
            end
        end

    elseif self.weapon == WPN_SG then
        self:firePellets(WPN_SG, 7, 3.0)

    elseif self.weapon == WPN_SSG then
        self:firePellets(WPN_SSG, 14, 5.5)

    elseif self.weapon == WPN_NG then
        local dx, dy, dz = fireDir(self.yaw_rad, self.pitch_rad)
        table.insert(self.projectiles, {
            x = ox, y = oy, z = oz,
            vx = dx * 24, vy = dy * 24, vz = dz * 24,
            type = "nail", life = 2.5,
        })

    elseif self.weapon == WPN_RL then
        local dx, dy, dz = fireDir(self.yaw_rad, self.pitch_rad)
        table.insert(self.projectiles, {
            x = ox, y = oy, z = oz,
            vx = dx * 16, vy = dy * 16, vz = dz * 16,
            type = "rocket", life = 5.0,
        })
    end
end

------------------------------------------------------------------------
-- Pellet hitscan (shotgun spread)
------------------------------------------------------------------------

function Q:firePellets(wpn, count, spreadDeg)
    local ox, oy, oz = self.px, self.py, self.pz

    for _ = 1, count do
        local yo = (math.random() - 0.5) * math.rad(spreadDeg)
        local po = (math.random() - 0.5) * math.rad(spreadDeg)
        local dx, dy, dz = fireDir(self.yaw_rad + yo, self.pitch_rad + po)

        local bestT = 60
        local bestE = nil

        for _, e in ipairs(self.enemies) do
            if e.alive then
                -- Project enemy centre onto the ray; check perpendicular distance
                local ex = e.x - ox
                local ey = (e.y + 1.1 * e.scale) - oy
                local ez = e.z - oz
                local t  = ex*dx + ey*dy + ez*dz
                if t > 0.2 and t < bestT then
                    local hx = ox + dx*t - e.x
                    local hy = oy + dy*t - (e.y + 1.1 * e.scale)
                    local hz = oz + dz*t - e.z
                    if math.sqrt(hx*hx + hy*hy + hz*hz) < 0.7 * e.scale then
                        bestT = t
                        bestE = e
                    end
                end
            end
        end

        if bestE then
            self:hitEnemy(bestE, math.floor(WEAPON_DAMAGE[wpn] / count))
            table.insert(self.effects, {
                x = ox + dx*bestT, y = oy + dy*bestT, z = oz + dz*bestT,
                t = 0.25, type = "spark",
            })
        end
    end
end

------------------------------------------------------------------------
-- Enemy damage
------------------------------------------------------------------------

function Q:hitEnemy(e, dmg)
    e.hp        = e.hp - dmg
    e.painTimer = 0.2
    if e.hp <= 0 then
        e.alive     = false
        e.respawnCd = 8
        self.kills  = self.kills + 1
        table.insert(self.effects, {
            x = e.x, y = e.y + 1, z = e.z, t = 0.9, type = "blast"
        })
        server.log(string.format("Killed %s!  Total kills: %d", e.type, self.kills))
    end
end

------------------------------------------------------------------------
-- Rocket splash damage
------------------------------------------------------------------------

function Q:rocketSplash(x, y, z)
    local radius = 5
    for _, e in ipairs(self.enemies) do
        if e.alive then
            local d = dist3(x, y, z, e.x, e.y + 1, e.z)
            if d < radius then
                local dmg = math.floor(WEAPON_DAMAGE[WPN_RL] * (1 - d / radius))
                if dmg > 0 then self:hitEnemy(e, dmg) end
            end
        end
    end
    -- Self-damage
    local sd = dist3(x, y, z, self.px, self.py, self.pz)
    if sd < radius then
        local dmg = math.floor(65 * (1 - sd / radius))
        if dmg > 0 then self:takeDamage(dmg, 255, 80, 0) end
    end
end

------------------------------------------------------------------------
-- Player takes damage / death
------------------------------------------------------------------------

function Q:takeDamage(amount, r, g, b)
    local abs = math.min(self.armor, math.floor(amount * 0.66))
    self.armor  = self.armor  - abs
    self.health = self.health - (amount - abs)
    self.flash  = { r = r or 200, g = g or 0, b = b or 0, a = 180, t = 0.35 }
    if self.health <= 0 then
        self.health    = 0
        self.dead      = true
        self.deadTimer = 0
        server.log("Player died!")
    end
end

function Q:respawn()
    self.px, self.py, self.pz = 0, 1.8, 6
    self.yaw_rad, self.pitch_rad = 0, 0   -- overwritten by player.getLook() next frame
    self.velY      = 0
    self.onGround  = true
    self.health    = 100
    self.armor     = 50
    self.ammo      = { shells = 25, nails = 30, rockets = 5 }
    self.weapon    = WPN_SG
    self.fireCd    = 0
    self.dead      = false
    self.deadTimer = 0
    self.projectiles = {}
    self.effects     = {}
    spawnEnemies()
    -- set player locatiion
    player.setPos(self.px, self.py, self.pz)
    server.log("Player respawned.")
end

------------------------------------------------------------------------
-- Projectiles
------------------------------------------------------------------------

function Q:updateProjectiles(dt)
    local keep = {}
    for _, p in ipairs(self.projectiles) do
        p.x    = p.x + p.vx * dt
        p.y    = p.y + p.vy * dt
        p.z    = p.z + p.vz * dt
        p.life = p.life - dt

        local dead = p.life <= 0

        -- Floor impact
        if not dead and p.y < 0.08 then
            p.y  = 0.08
            dead = true
            if p.type == "rocket" then
                self:rocketSplash(p.x, p.y, p.z)
                table.insert(self.effects, { x=p.x, y=0.1, z=p.z, t=0.9, type="blast" })
            else
                table.insert(self.effects, { x=p.x, y=0.1, z=p.z, t=0.22, type="spark" })
            end
        end

        -- Wall impact
        if not dead and (math.abs(p.x) > 19.7 or p.z < -21.7 or p.z > 9.7) then
            dead = true
            if p.type == "rocket" then
                self:rocketSplash(p.x, p.y, p.z)
                table.insert(self.effects, { x=p.x, y=p.y, z=p.z, t=0.9, type="blast" })
            else
                table.insert(self.effects, { x=p.x, y=p.y, z=p.z, t=0.2, type="spark" })
            end
        end

        -- Enemy hit
        if not dead then
            for _, e in ipairs(self.enemies) do
                if e.alive then
                    if dist3(p.x, p.y, p.z, e.x, e.y + 1, e.z) < 0.85 * e.scale then
                        dead = true
                        if p.type == "rocket" then
                            self:rocketSplash(p.x, p.y, p.z)
                            table.insert(self.effects, { x=p.x, y=p.y, z=p.z, t=0.9, type="blast" })
                        else
                            self:hitEnemy(e, WEAPON_DAMAGE[WPN_NG])
                            table.insert(self.effects, { x=p.x, y=p.y, z=p.z, t=0.25, type="spark" })
                        end
                        break
                    end
                end
            end
        end

        if not dead then table.insert(keep, p) end
    end
    self.projectiles = keep
end

------------------------------------------------------------------------
-- Hit effects (sparks + explosions)
------------------------------------------------------------------------

function Q:updateEffects(dt)
    local keep = {}
    for _, e in ipairs(self.effects) do
        e.t = e.t - dt
        if e.t > 0 then table.insert(keep, e) end
    end
    self.effects = keep
end

------------------------------------------------------------------------
-- Enemy AI (oscillation + proximity melee)
------------------------------------------------------------------------

function Q:updateEnemies(dt)
    local t = GetTime()
    for _, e in ipairs(self.enemies) do
        if not e.alive then
            e.respawnCd = e.respawnCd - dt
            if e.respawnCd <= 0 then
                e.alive     = true
                e.hp        = e.maxHp
                e.painTimer = 0
            end
        else
            e.rot       = (e.rot + 50 * dt) % 360
            e.painTimer = math.max(0, e.painTimer - dt)

            -- Simple proximity melee attack (once per ~1.5 s, staggered by id)
            if dist2(self.px, self.pz, e.x, e.z) < 1.6 then
                local phase = math.floor((t + e.id * 0.37) / 1.5)
                if phase ~= e.atkPhase then
                    e.atkPhase = phase
                    self:takeDamage(ENEMY_DEFS[e.type].melee, 200, 0, 0)
                end
            end
        end
        -- lerp enimies around the map in a slow bobbing pattern to give them some life
        local def = ENEMY_DEFS[e.type]
        local bob = math.sin(math.rad(e.rot) * 3 + t * 4) * 0.04
        e.y = bob + 0.1 * math.sin(t * 1.2 + e.id) + 0.1 * math.cos(t * 0.7 + e.id * 1.7)
        
        for _, tch in ipairs(self.torches) do
            local flicker = 0.85
                + 0.22 * math.sin(t * 7.1  + tch.phase)
                + 0.14 * math.sin(t * 13.7 + tch.phase * 1.7)
            lighting.setIntensity(tch.handle, flicker)
        end

        -- -- lerp the enimies around the map
        --     local radius = 0.5 * def.scale
        --     local speed = 0.5 / def.scale
        --     local angle = math.rad(e.rot)
        --     e.x = e.x + math.cos(angle) * speed * dt
        --     e.z = e.z + math.sin(angle) * speed * dt
    
        --     -- keep enimies within the arena bounds
        --     if e.x < -19 + radius then e.x = -19 + radius end
        --     if e.x > 19 - radius then e.x = 19 - radius end
        --     if e.z < -21 + radius then e.z = -21 + radius end
        --     if e.z > 9 - radius then e.z = 9 - radius end

        -- find the location of the player
        local playerX, playerY, playerZ = self.px, self.py, self.pz
        -- setup ai to move towards the player
        local speed = 1.0 * def.scale
        local angleToPlayer = math.atan2(playerZ - e.z, playerX - e.x)
        e.x = e.x + math.cos(angleToPlayer) * speed * dt
        e.z = e.z + math.sin(angleToPlayer) * speed * dt


    end
end

------------------------------------------------------------------------
-- Dynamic lighting
------------------------------------------------------------------------

function Q:updateLighting(dt)
    local t = GetTime()

    -- Muzzle flash
    if self.muzzleLight then
        if self.muzzleTimer > 0 then
            local dx, _, dz = fireDir(self.yaw_rad, self.pitch_rad)
            lighting.setPos(self.muzzleLight,
                self.px + dx * 0.9, self.py - 0.1, self.pz + dz * 0.9)
            lighting.setIntensity(self.muzzleLight,
                3.5 * (self.muzzleTimer / 0.12))
        else
            lighting.setIntensity(self.muzzleLight, 0)
        end
    end

    -- Torch flicker
    for _, tch in ipairs(self.torches) do
        local flicker = 0.85
            + 0.22 * math.sin(t * 7.1  + tch.phase)
            + 0.14 * math.sin(t * 13.7 + tch.phase * 1.7)
        lighting.setIntensity(tch.handle, flicker)
    end
end

------------------------------------------------------------------------
-- Screen flash decay
------------------------------------------------------------------------

function Q:updateFlash(dt)
    if self.flash.t > 0 then
        self.flash.t = self.flash.t - dt
        self.flash.a = math.max(0, math.floor(180 * (self.flash.t / 0.35)))
    end
end

------------------------------------------------------------------------
-- draw3D  (called inside BeginMode3D / EndMode3D — mesh.* only)
------------------------------------------------------------------------

function Q:draw3D()

    -- ── Arena geometry ───────────────────────────────────────────────

    local WC = { 56, 48, 42 }   -- wall colour

    -- Floor
    mesh.plane(0, 0, -6, 40, 32, 68, 60, 54, 255)

    -- Outer walls
    mesh.box( 0,  3.5, -22,  40, 7, 1,   WC[1], WC[2], WC[3], 255)  -- north
    mesh.box( 0,  3.5,  10,  40, 7, 1,   WC[1], WC[2], WC[3], 255)  -- south
    mesh.box(-20, 3.5,  -6,   1, 7, 32,  WC[1], WC[2], WC[3], 255)  -- west
    mesh.box( 20, 3.5,  -6,   1, 7, 32,  WC[1], WC[2], WC[3], 255)  -- east

    -- Ceiling (slightly lighter)
    mesh.plane(0, 7, -6, 40, 32, 40, 34, 30, 255)

    -- Columns with torch brackets
    local cols = { { 6, -5 }, { -6, -5 }, { 6, -15 }, { -6, -15 } }
    for _, c in ipairs(cols) do
        local cx, cz = c[1], c[2]
        mesh.cylinder(cx, 0,   cz, 0.44, 0.56, 6,    12, 78, 68, 60, 255)  -- shaft
        mesh.box(     cx, 0.2, cz, 1.2,  0.4,  1.2,  88, 76, 66, 255)      -- plinth
        mesh.box(     cx, 6.0, cz, 1.4,  0.5,  1.4,  96, 84, 74, 255)      -- capital
        -- Torch bracket & flame glow sphere
        local tz = cz - 0.65
        mesh.box(    cx, 2.2, tz,  0.1, 0.1, 0.4,  72, 55, 35, 255)
        mesh.sphere( cx, 2.2, tz - 0.12, 0.14, 5, 5, 255, 155, 38, 255)
    end

    -- Central raised platform + steps
    mesh.box(0, 0.6, -11,   8, 1.2, 8,    88, 78, 68, 255)
    mesh.box(0, 1.25, -11, 7.8, 0.15, 7.8, 108, 96, 82, 255)  -- top highlight
    for i = 0, 2 do
        mesh.box(0, 0.2 + i*0.35, -7.3 - i*0.4,
                 3.5, 0.35, 0.5,  83, 72, 63, 255)
    end

    -- Alcoves in side walls
    mesh.box(-19, 3.5, -10,  0.25, 5, 7,  46, 40, 36, 255)
    mesh.box( 19, 3.5, -10,  0.25, 5, 7,  46, 40, 36, 255)

    -- Archway frame at south entrance
    mesh.box( 0,  3.5, 9, 6, 7, 1,  WC[1], WC[2], WC[3], 255)
    mesh.box( 0,  6.5, 9, 3, 1, 1,  68, 58, 48, 255)

    -- Crates (ammo / health props)
    local crates = {
        { x =  15, z = -2,   s = 1.1 },
        { x = -15, z = -18,  s = 1.0 },
        { x =   3, z = -20,  s = 1.2 },
        { x =  -3, z = -20,  s = 1.0 },
    }
    for _, cr in ipairs(crates) do
        mesh.box(     cr.x, cr.s * 0.5, cr.z, cr.s, cr.s, cr.s,       98, 74, 36, 255)
        mesh.boxWires(cr.x, cr.s * 0.5, cr.z, cr.s+0.02, cr.s+0.02, cr.s+0.02, 138, 108, 52, 255)
    end

    -- ── Enemies ─────────────────────────────────────────────────────

    for _, e in ipairs(self.enemies) do
        if e.alive then
            local def = ENEMY_DEFS[e.type]
            local er, eg, eb = def.color[1], def.color[2], def.color[3]
            if e.painTimer > 0 then er, eg, eb = 255, 48, 48 end  -- pain flash
            local s  = e.scale
            local ey = e.y

            if e.type == "ogre" then
                -- Stocky beast
                mesh.box(e.x, ey+1.3*s, e.z, 1.1*s, 1.6*s, 0.75*s, er,eg,eb, 255)
                mesh.box(e.x, ey+2.3*s, e.z, 0.8*s, 0.7*s, 0.70*s, er+14,eg+7,eb, 255)
                -- Red eyes
                mesh.box(e.x-0.18*s, ey+2.45*s, e.z-0.38*s, 0.16*s,0.10*s,0.04, 255,35,0,255)
                mesh.box(e.x+0.18*s, ey+2.45*s, e.z-0.38*s, 0.16*s,0.10*s,0.04, 255,35,0,255)
                -- Swinging arms
                local sw = math.sin(math.rad(e.rot) * 2) * 0.45
                mesh.box(e.x-0.76*s, ey+1.1*s+sw, e.z, 0.32*s,0.9*s,0.32*s, er-14,eg-7,eb, 255)
                mesh.box(e.x+0.76*s, ey+1.1*s-sw, e.z, 0.32*s,0.9*s,0.32*s, er-14,eg-7,eb, 255)
                -- Club
                mesh.cylinder(e.x+0.76*s, ey+0.6*s-sw, e.z, 0.08, 0.14, 0.7, 6, 58,38,22,255)

            elseif e.type == "knight" then
                -- Armoured warrior
                mesh.box(e.x, ey+1.2*s, e.z, 0.9*s,1.4*s,0.55*s, er,eg,eb, 255)
                -- Pauldrons
                mesh.box(e.x-0.60*s, ey+1.8*s, e.z, 0.30*s,0.20*s,0.45*s, er+30,eg+30,eb+28, 255)
                mesh.box(e.x+0.60*s, ey+1.8*s, e.z, 0.30*s,0.20*s,0.45*s, er+30,eg+30,eb+28, 255)
                -- Helmet
                mesh.box(e.x, ey+2.2*s, e.z, 0.60*s,0.60*s,0.60*s, er+38,eg+38,eb+36, 255)
                -- Visor slit (gold)
                mesh.box(e.x, ey+2.22*s, e.z-0.32*s, 0.42*s,0.10*s,0.05, 175,135,0,255)
                -- Sword swing
                local sw = math.sin(math.rad(e.rot) * 2) * 0.5
                mesh.box(e.x+0.60*s+sw*0.3, ey+1.0*s, e.z-0.30*s, 0.08,1.4*s,0.08, 198,200,218,255)
                mesh.box(e.x+0.60*s+sw*0.3, ey+1.5*s, e.z-0.30*s, 0.26,0.06,0.06, 155,98,35,255)

            elseif e.type == "soldier" then
                -- Green-uniformed trooper
                mesh.box(e.x, ey+1.0*s, e.z, 0.75*s,1.2*s,0.50*s, er,eg,eb, 255)
                mesh.sphere(e.x, ey+2.0*s, e.z, 0.30*s, 6,6, er+24,eg+15,eb+8, 255)
                -- Helmet
                mesh.cylinder(e.x, ey+1.88*s, e.z, 0.30*s,0.34*s,0.22*s, 8, 48,63,48,255)
                -- Gun arm + barrel
                mesh.box(e.x-0.50*s, ey+0.9*s, e.z-0.20*s, 0.20*s,0.50*s,0.20*s, 38,52,38,255)
                mesh.cylinder(e.x-0.50*s, ey+0.62*s, e.z-0.36*s, 0.04,0.04,0.38, 6, 32,32,32,255)

            else  -- grunt
                mesh.box(e.x, ey+0.9*s, e.z, 0.65*s,1.0*s,0.45*s, er,eg,eb, 255)
                mesh.sphere(e.x, ey+1.75*s, e.z, 0.28*s, 5,5, er+18,eg+13,eb+5, 255)
                -- Yellow eyes
                mesh.box(e.x-0.10*s, ey+1.80*s, e.z-0.29*s, 0.10*s,0.08*s,0.03, 228,198,28,255)
                mesh.box(e.x+0.10*s, ey+1.80*s, e.z-0.29*s, 0.10*s,0.08*s,0.03, 228,198,28,255)
            end

            -- HP bar above head
            local hfrac = e.hp / e.maxHp
            local bw    = 1.4 * s
            local bx    = e.x - bw * (1 - hfrac) * 0.5
            mesh.box(e.x, ey+3.3*s, e.z, bw,          0.13, 0.06, 45,15,15,200)
            if hfrac > 0 then
                mesh.box(bx, ey+3.3*s, e.z, bw*hfrac, 0.14, 0.07,
                    math.floor(255*(1-hfrac)), math.floor(255*hfrac), 0, 218)
            end

        else
            -- Corpse slab (visible while respawn timer is still high)
            if e.respawnCd > 6 then
                mesh.box(e.x, 0.1, e.z, 1.4*e.scale, 0.2, 0.9*e.scale, 75,12,8,155)
            end
        end
    end

    -- ── Projectiles ──────────────────────────────────────────────────

    for _, p in ipairs(self.projectiles) do
        if p.type == "rocket" then
            mesh.box(p.x, p.y, p.z, 0.13, 0.13, 0.46, 215,175,38,255)
            -- Exhaust glow (trail behind rocket)
            local tdx = -p.vx / 16
            local tdz = -p.vz / 16
            mesh.sphere(p.x + tdx*0.28, p.y, p.z + tdz*0.28, 0.19, 5,5, 255,75,8,195)
        else  -- nail
            mesh.sphere(p.x, p.y, p.z, 0.07, 4,4, 255,228,55,255)
        end
    end

    -- ── Hit effects ──────────────────────────────────────────────────

    for _, ef in ipairs(self.effects) do
        if ef.type == "blast" then
            local frac = ef.t / 0.9
            local r1   = (1 - frac) * 4.0
            -- Outer fireball
            mesh.sphere(ef.x, ef.y, ef.z, r1+0.05, 8,8,
                255, math.floor(155*frac+55), 8, math.floor(225*frac))
            -- Inner hot core
            mesh.sphere(ef.x, ef.y, ef.z, r1*0.44, 6,6,
                255, 238, 95, math.floor(195*frac))
            -- Smoke ring
            mesh.cylinder(ef.x, ef.y, ef.z, r1*0.8, r1*1.1, 0.22, 10,
                78,68,58, math.floor(95*frac))
        else  -- spark
            local frac = ef.t / 0.25
            for si = 1, 6 do
                local a  = si*60 + ef.t * 1200
                local sr = (1 - frac) * 1.1
                mesh.sphere(
                    ef.x + math.cos(math.rad(a)) * sr,
                    ef.y + (1 - frac) * 0.55,
                    ef.z + math.sin(math.rad(a)) * sr,
                    0.055, 3,3, 255,208,55, math.floor(195*frac))
            end
        end
    end

    -- ── Remote (multiplayer) players ─────────────────────────────────

    for _, p in ipairs(network.getPlayers()) do
        if p.id ~= network.getLocalId() then
            mesh.box(   p.x, p.y+0.9, p.z, 0.6,1.8,0.5, 58,98,198,200)
            mesh.sphere(p.x, p.y+2.2, p.z, 0.32, 6,6, 196,176,136,255)
        end
    end
end

------------------------------------------------------------------------
-- Draw  (2D HUD — called after EndMode3D; render.* only)
------------------------------------------------------------------------

function Q:Draw()

    -- ── Death screen ────────────────────────────────────────────────
    if self.dead then
        render.drawRect(0, 0, 1280, 720, 115, 0, 0, 165)
        render.drawText("YOU DIED",  450, 285, 74, 218,18,18,255)
        local secs = math.ceil(3 - self.deadTimer)
        render.drawText("Respawning in " .. secs .. "...", 512, 382, 22, 176,155,135,255)
        render.drawText("Kills this run: " .. self.kills,  560, 424, 17, 198,158,75,255)
        return
    end

    -- ── Damage flash overlay ─────────────────────────────────────────
    if self.flash.a > 0 then
        render.drawRect(0, 0, 1280, 720,
            self.flash.r, self.flash.g, self.flash.b, self.flash.a)
    end

    -- ── Bottom status bar ────────────────────────────────────────────
    render.drawRect(0,   652, 1280, 68,  16,12,8,  225)
    render.drawRect(0,   650, 1280,  2,  68,52,32, 255)

    -- Health
    render.drawText("HEALTH", 16, 657, 11,  108,90,70,255)
    local hc = self.health > 60 and {88,208,68} or
               self.health > 25 and {208,168,32} or {212,52,38}
    render.drawText(tostring(math.max(0,self.health)), 16, 669, 26,
        hc[1],hc[2],hc[3],255)

    -- Armorf
    render.drawRect(110, 655, 108, 42,  26,20,14,172)
    render.drawText("ARMOR", 118, 657, 11,  108,90,70,255)
    render.drawText(tostring(math.max(0,self.armor)), 118, 669, 26,
        98,152,212,255)

    -- Weapon name (centred)
    local wname = WEAPON_NAMES[self.weapon]
    render.drawText(wname, math.floor(640 - #wname * 5), 658, 17,  255,193,42,255)

    -- Ammo counter
    local atype = WEAPON_AMMO[self.weapon]
    local acnt  = atype and (self.ammo[atype] or 0) or -1
    render.drawText("AMMO", 1108, 657, 11,  108,90,70,255)
    if acnt >= 0 then
        local ac = acnt > 5 and {255,212,72} or {212,52,38}
        render.drawText(tostring(acnt), 1108, 669, 26, ac[1],ac[2],ac[3],255)
    else
        render.drawText("--", 1108, 671, 22, 128,118,108,255)
    end

    -- Ammo totals (bottom-right corner)
    render.drawRect(1200, 638, 80, 82,  16,12,8,192)
    render.drawText("SH "..self.ammo.shells,  1204,641,12, 188,168,92,255)
    render.drawText("NL "..self.ammo.nails,   1204,657,12, 188,168,92,255)
    render.drawText("RK "..self.ammo.rockets, 1204,673,12, 188,168,92,255)

    -- Kill counter (top-right)
    render.drawRect(1188, 0, 92, 44,  16,12,8,192)
    render.drawText("KILLS",          1193, 4,  12, 148,128,92,255)
    render.drawText(tostring(self.kills), 1193, 18, 21, 255,198,42,255)

    -- ── Weapon list (top-left) ───────────────────────────────────────
    render.drawRect(0, 0, 202, 128,  0,0,0,148)
    for i = 1, 5 do
        local sel    = (i == self.weapon)
        local at     = WEAPON_AMMO[i]
        local hasAmm = (not at) or ((self.ammo[at] or 0) >= WEAPON_COST[i])
        local cr, cg, cb
        if sel        then cr,cg,cb = 255,213,42
        elseif hasAmm then cr,cg,cb = 138,124,102
        else               cr,cg,cb = 70,62,54
        end
        render.drawText(i.."  "..WEAPON_NAMES[i], 8, 4+(i-1)*23, 14, cr,cg,cb,255)
    end

    -- ── Crosshair ────────────────────────────────────────────────────
    local cx, cy = 640, 360
    local gap, len = 5, 10

    -- Shadow bars
    render.drawRect(cx-gap-len+1, cy,        len, 2, 0,0,0,155)
    render.drawRect(cx+gap+1,     cy,        len, 2, 0,0,0,155)
    render.drawRect(cx,        cy-gap-len+1, 2, len, 0,0,0,155)
    render.drawRect(cx,        cy+gap+1,     2, len, 0,0,0,155)

    -- Colour: green normally, bright yellow when firing
    local xhR = self.muzzleTimer > 0 and 255 or  35
    local xhG = self.muzzleTimer > 0 and 255 or 228
    local xhB = self.muzzleTimer > 0 and   0 or  55

    render.drawRect(cx-gap-len, cy-1,     len, 2, xhR,xhG,xhB,238)
    render.drawRect(cx+gap,     cy-1,     len, 2, xhR,xhG,xhB,238)
    render.drawRect(cx-1, cy-gap-len,     2, len, xhR,xhG,xhB,238)
    render.drawRect(cx-1, cy+gap,         2, len, xhR,xhG,xhB,238)
    render.drawRect(cx-1, cy-1,           2,   2, xhR,xhG,xhB,255)  -- dot

    -- ── Centre health bar ────────────────────────────────────────────
    local hbw = 180
    local hbx = 640 - math.floor(hbw / 2)
    render.drawRect(hbx, 632, hbw, 9,  38,8,8,205)
    local fill = math.floor(hbw * (self.health / 100))
    if fill > 0 then
        render.drawRect(hbx, 632, fill, 9,
            math.floor(218*(1-self.health/100)),
            math.floor(218*(self.health/100)),
            28, 228)
    end

    -- ── Header bar ───────────────────────────────────────────────────
    render.drawRect(488, 0, 304, 21,  0,0,0,132)
    render.drawText(
        string.format("QUAKE DEMO  |  t=%.0fs", GetTime()),
        492, 3, 13,  158,138,98,255)

    -- ── Controls hint ────────────────────────────────────────────────
    render.drawText(
        "WASD=Move  MOUSE=Look  LMB=Fire  1-5/Wheel=Weapon  SPACE=Jump  LSHIFT=Run  R=Respawn",
        180, 706, 11, 108,98,82,255)
end

------------------------------------------------------------------------
-- Network events
------------------------------------------------------------------------

function Q:onPlayerJoined(id, name)
    server.log(string.format("++ [%d] %s joined", id, name))
end

function Q:onPlayerLeft(id)
    server.log(string.format("-- [%d] left", id))
end
