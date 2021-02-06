'use strict';

import {MTLLoader} from './3p/three.js/MTLLoader.js';
import {OBJLoader} from './3p/three.js/OBJLoader.js';
import * as THREE from './3p/three.js/three.module.js';

////////////////////////////////////////////////////////////////////////////////
// REQUESTS
////////////////////////////////////////////////////////////////////////////////

// Performs an XHR to get the given URL.
function getUrl(url, onLoad, onError) {
  const r = new XMLHttpRequest();
  r.addEventListener('load', () => onLoad(r.response));
  r.addEventListener('error', () => onError(r.status, r.statusText));
  r.open('GET', url);
  r.send();
}

// Performs an XHR to get a URL and parses the result as JSON.
function getJson(url, onLoad, onError) {
  getUrl(url, result => onLoad(JSON.parse(result)), onError);
}

// Extracts the filenames (absolute path) from an HTML directory index.
function listDir(url, onLoad, onError) {
  getUrl(
      url,
      result =>
          onLoad([...result.matchAll(/<a href="(\S*)">/mg)].map(a => a[1])),
      onError);
}

////////////////////////////////////////////////////////////////////////////////
// MATERIALS
////////////////////////////////////////////////////////////////////////////////

// In Threejs, materials may be arrays, single objects, or null.
function forEachMaterial(obj, func) {
  if (obj.material) {
    if (obj.material instanceof Array) {
      obj.material.forEach(func);
    } else {
      func(obj.material);
    }
  }
}

// Returns the set of all nested materials.
function getNestedMaterials(obj) {
  const out = new Set();
  if (obj) {
    obj.traverse(child => forEachMaterial(child, mat => out.add(mat)));
  }
  return out;
}

// Traverses and replaces all materials owned by this object.
function replaceNestedMaterials(obj, func) {
  const map = new Map();
  const getOrReplace = m => {
    let replacement = map.get(m);
    if (!replacement) {
      replacement = func(m);
      map.set(m, replacement);
    }
    return replacement;
  };
  obj.traverse(child => {
    if (child.material) {
      if (child.material instanceof Array) {
        child.material = child.material.map(getOrReplace);
      } else {
        child.material = getOrReplace(child.material);
      }
    }
  });
}

////////////////////////////////////////////////////////////////////////////////
// BACKDROPS
////////////////////////////////////////////////////////////////////////////////

function createSoftCircleTexture(width) {
  const data = new Uint8Array(width * width);
  const c = 0.5 * (width - 1);
  const k = 1.0 / c;
  for (let y = 0; y < width; ++y) {
    const dy = y - c;
    for (let x = 0; x < width; ++x) {
      const i = y * width + x;
      const dx = x - c;
      const l = k * k * (dx * dx + dy * dy);
      data[i] = Math.max(0, 255 * (1 - l));
    }
  }
  return new THREE.DataTexture(data, width, width, THREE.LuminanceFormat);
}

// Creates a 'garage' backdrop with lights and background.
function createGarage() {
  const out = {
    visual: new THREE.Group(),
    background: new THREE.Color().setHSL(0.0, 0.0, 0.1),
  };

  // Hemisphere light.
  const hemiLight = new THREE.HemisphereLight(0xffffff, 0x666666, 1.3);
  out.visual.add(hemiLight);

  // A target for the right lights.
  const lightTarget = new THREE.Object3D();
  lightTarget.position.set(0, 1, 0);
  out.visual.add(lightTarget);

  // Ring lights.
  const kNumLights = 6;
  const kLightColor = 0xffffff;
  const kLightIntensity = 0.3;

  const dTheta = 2.0 * Math.PI / kNumLights;
  let theta = 0.0;
  for (let i = 0; i < kNumLights; ++i, theta += dTheta) {
    const dirLight = new THREE.DirectionalLight(kLightColor, kLightIntensity);
    dirLight.position.set(
        6.0 * Math.cos(theta), 5 * (i % 2) + 1.5, 6.0 * Math.sin(theta));
    dirLight.target = lightTarget;
    out.visual.add(dirLight);
    // const helper = new THREE.DirectionalLightHelper(dirLight);
    // out.group.add(helper);
  }

  // Floor.
  const floor_geom = new THREE.CircleGeometry(7, 32);
  const floor_mat = new THREE.MeshBasicMaterial({color: 0x080808});
  const floor = new THREE.Mesh(floor_geom, floor_mat);
  floor_mat.alphaMap = createSoftCircleTexture(256);
  floor_mat.transparent = true;
  floor.rotation.x = -0.5 * Math.PI;
  out.visual.add(floor);

  return out;
}

////////////////////////////////////////////////////////////////////////////////
// MODELS (CARS)
////////////////////////////////////////////////////////////////////////////////

class Model {
  constructor() {
    this.currentLod = 0;
    this.currentSkin = 0;
    this.lods = [];   // THREE.Object3D()
    this.skins = [];  // THREE.Texture()
  }

  getVisual() {
    return this.lods[this.currentLod];
  }

  updateLod(i, lod) {
    while (this.lods.length < i + 1) this.lods.push(new THREE.Object3D());
    // Add a fake shadow.
    const shadow = lod.clone();
    replaceNestedMaterials(shadow, m => {
      return new THREE.MeshBasicMaterial({
        color: 0x010101,
        polygonOffset: true,
        polygonOffsetFactor: -1,
      });
    });
    shadow.scale.y = 0;
    // Place the wheels on the ground.
    const box = new THREE.Box3().setFromObject(lod);
    lod.position.y = -box.min.y;
    // Store everything.
    const g = new THREE.Group();
    g.add(lod);
    g.add(shadow);
    this.lods[i] = g;
  }

  updateSkin(i, tex) {
    while (this.skins.length < i + 1) this.skins.push(null);
    this.skins[i] = tex;
    this.pickSkin(this.currentSkin);
  }

  pickSkin(i) {
    i = Math.min(this.skins.length - 1, Math.max(0, i));
    if (i != this.currentSkin) {
      for (let lod of this.lods) {
        for (let mat of getNestedMaterials(lod)) {
          if (mat.map != this.skins[i]) {
            mat.map = this.skins[i];
            mat.needsUpdate = true;
          }
        }
      }
      this.currentSkin = i;
    }
  }

  nextSkin() {
    this.pickSkin(this.currentSkin + 1);
  }

  priorSkin() {
    this.pickSkin(this.currentSkin - 1);
  }
};

////////////////////////////////////////////////////////////////////////////////
// EVERYTHING NICE
////////////////////////////////////////////////////////////////////////////////

class World {
  constructor() {
    this.scene = new THREE.Scene();

    this.renderer = new THREE.WebGLRenderer({antialias: true});
    this.renderer.autoClear = false;
    this.renderer.outputEncoding = THREE.sRGBEncoding;
    document.body.appendChild(this.renderer.domElement);

    this.camera = new THREE.PerspectiveCamera(1, 1, 0.1, 100.0);
    this.camera.position.set(-12, 3, -12);
    this.camera.lookAt(new THREE.Vector3(0, 1, 0));
    this.setSize(window.innerWidth, window.innerHeight);

    this.backdrop = null;  // Contains field .group of type THREE.Object3d.
    // Currently loading or loaded model.
    this.model = null;  // Contains several lods of type THREE.Object3d.
    // Currently displayed visual (may lag behind 'model' if loading).
    this.visual = null;  // Is a THREE.Object3d.

    // Last time 'update()' was called.
    this.lastT = 0;
    this.rotation = 0.0;
  }

  // Sets the size of the camera and the renderer. Respects min x/y fovs.
  setSize(width, height) {
    const fovX = 40;
    const aspect = width / height;
    const fovY = Math.max(15, fovX / aspect);
    this.camera.aspect = aspect;
    this.camera.fov = fovY;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height);
  }

  // backdrop {
  //   group : THREE.Object3d or THREE.Group3d
  //   background : THREE.Color (anything compat with THREE.Scene.background).
  // }
  setBackdrop(backdrop) {
    if (this.backdrop) {
      this.scene.remove(this.backdrop.visual);
    }
    this.backdrop = backdrop;
    this.scene.add(this.backdrop.visual);
    this.scene.background = backdrop.background;
  }

  // Sets the current Object3d or Group to be displayed.
  setVisual(visual) {
    if (this.visual) this.scene.remove(this.visual);
    if (visual) this.scene.add(visual);
    this.visual = visual;
  }

  // t: timestamp milliseconds (i.e. what's returned by requestAnimationFrame).
  update(t) {
    let dt = 0.0;
    if (t) {
      dt = 0.001 * Math.min(200, t - this.lastT);
      this.lastT = t;
    }
    // Rotate the model.
    this.rotation += 0.5 * dt;
    if (this.visual) {
      this.visual.rotation.y = this.rotation;
    }
  }

  // Draws the background, reflections, model, etc.
  draw() {
    const flip = o => {
      if (o) o.scale.y = -o.scale.y;
    };
    const popBackground = s => {
      const out = s.background;
      s.background = null;
      return out;
    };

    this.renderer.clear();

    // Draw reflections.
    flip(this.visual);
    flip(this.backdrop.visual);
    var mats = getNestedMaterials(this.visual);
    const colors = new Map();
    for (let m of mats) {
      colors.set(m, m.color.clone());
      m.color.multiplyScalar(0.2);
    }
    this.renderer.render(this.scene, this.camera);
    for (let [m, color] of colors) {
      m.color = color;
    }
    flip(this.backdrop.visual);
    flip(this.visual);

    // Draw the main scene.
    const bg = popBackground(this.scene);
    this.renderer.render(this.scene, this.camera);
    this.scene.background = bg;
  }

  loadCar(name) {
    function setCarTexParams(tex) {
      tex.encoding = THREE.sRGBEncoding;
      tex.magFilter = THREE.NearestFilter;
      tex.minFilter = THREE.NearestFilter;
    };

    const path = name;
    const model = new Model();

    // Loads the json listing for this model.
    getJson(
        path + 'o.json',
        result => {
          // The first skin is loaded from the .mtl.
          for (let i = 1; i < result.palettes; ++i) {
            const tex_path = path + 'p.' + i + '.png';
            const tex = new THREE.TextureLoader().load(tex_path);
            setCarTexParams(tex);
            model.updateSkin(i, tex);
          }
        },
        (code, text) => {
          console.log('Request failed', code, text);
        });

    // Load the MTL first, otherwise the OBJ doesn't races with the texture...
    new MTLLoader().load(path + 'o.mtl', m => {
      const objLoader = new OBJLoader();
      objLoader.setMaterials(m);
      objLoader.load(path + 'o.0.obj', o => {
        for (let m of getNestedMaterials(o)) {
          m.alphaTest = 0.005;
          if (m.map) {
            setCarTexParams(m.map);
            model.updateSkin(0, m.map);
          }
        }
        model.updateLod(0, o);
        // Only update the currently displayed visual if this is still the model
        // we want. A new one may have been requested already.
        if (this.model === model) {
          this.setVisual(model.getVisual());
        }
      });
    });

    // This is the currently loading model. May be replaced before it's actually
    // complete.
    this.model = model;
  }
};

////////////////////////////////////////////////////////////////////////////////
// INTERACTIONS
////////////////////////////////////////////////////////////////////////////////

// Setup.
const world = new World();
world.setBackdrop(createGarage());

// Load some models.
let current_model = 0;
let models = [];
listDir('/models/', result => {
  // Load everything that looks like .cdo.0.obj (or cno.0.obj).
  models = result.map(n => n.match(/(.*\.c.+)o\.0\.obj/))
               .filter(n => n)
               .map(n => n[1])
               .sort();
  if (models) {
    world.loadCar(models[current_model]);
  }
}, error => console.error('Failed to list models.', error));

// Handle resizing.
window.addEventListener('resize', e => {
  world.setSize(window.innerWidth, window.innerHeight);
});

// Allow switching some car parameters.
window.addEventListener('keydown', e => {
  if (world.model) {
    switch (e.code) {
      case 'KeyW':
        current_model = Math.min(current_model + 1, models.length - 1);
        world.loadCar(models[current_model]);
        break;
      case 'KeyS':
        current_model = Math.max(current_model - 1, 0);
        world.loadCar(models[current_model]);
        break;
      case 'KeyA':
        world.model.priorSkin();
        break;
      case 'KeyD':
        world.model.nextSkin();
        break;
    }
  }
});

// Render loop.
(function onFrame(t) {
  window.requestAnimationFrame(onFrame);
  world.update(t);
  world.draw();
})(/*t=*/0);
