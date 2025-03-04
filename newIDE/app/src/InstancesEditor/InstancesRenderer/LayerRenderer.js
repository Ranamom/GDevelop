// @flow
import gesture from 'pixi-simple-gesture';
import ObjectsRenderingService from '../../ObjectsRendering/ObjectsRenderingService';
import RenderedInstance from '../../ObjectsRendering/Renderers/RenderedInstance';
import getObjectByName from '../../Utils/GetObjectByName';
import ViewPosition from '../ViewPosition';

import * as PIXI from 'pixi.js-legacy';
import * as THREE from 'three';
import { shouldBeHandledByPinch } from '../PinchHandler';
import { makeDoubleClickable } from './PixiDoubleClickEvent';
import Rectangle from '../../Utils/Rectangle'; // TODO (3D): add support for zMin/zMax/depth.
import { rotatePolygon, type Polygon } from '../../Utils/PolygonHelper';
import Rendered3DInstance from '../../ObjectsRendering/Renderers/Rendered3DInstance';
const gd: libGDevelop = global.gd;

export default class LayerRenderer {
  project: gdProject;
  instances: gdInitialInstancesContainer;
  layout: gdLayout;
  /** `layer` can be changed at any moment (see InstancesRenderer).
   * /!\ Don't store any other reference.
   */
  layer: gdLayer;
  viewPosition: ViewPosition;
  onInstanceClicked: gdInitialInstance => void;
  onInstanceRightClicked: ({|
    offsetX: number,
    offsetY: number,
    x: number,
    y: number,
  |}) => void;
  onInstanceDoubleClicked: gdInitialInstance => void;
  onOverInstance: gdInitialInstance => void;
  onOutInstance: gdInitialInstance => void;
  onMoveInstance: (gdInitialInstance, number, number) => void;
  onMoveInstanceEnd: void => void;
  onDownInstance: (gdInitialInstance, number, number) => void;
  /**Used for instances culling on rendering */
  viewTopLeft: [number, number];
  /** Used for instances culling on rendering */
  viewBottomRight: [number, number];

  renderedInstances: { [number]: RenderedInstance | Rendered3DInstance } = {};
  pixiContainer: PIXI.Container;

  /** Functor used to render an instance */
  instancesRenderer: gdInitialInstanceJSFunctor;

  wasUsed: boolean = false;

  _temporaryRectangle: Rectangle = new Rectangle();
  _temporaryRectanglePath: Polygon = [[0, 0], [0, 0], [0, 0], [0, 0]];

  /**
   * The render texture where the whole 2D layer is rendered.
   * The render texture is then used for lighting (if it's a light layer)
   * or to be rendered in a 3D scene (for a 2D+3D layer).
   */
  _renderTexture: PIXI.RenderTexture | null = null;

  // Width and height are tracked when a render texture is used.
  _oldWidth: number | null = null;
  _oldHeight: number | null = null;

  // For a 3D (or 2D+3D) layer:
  _threeGroup: THREE.Group | null = null;
  _threeScene: THREE.Scene | null = null;
  _threeCamera: THREE.PerspectiveCamera | null = null;

  // For a 2D+3D layer, the 2D rendering is done on the render texture
  // and then must be displayed on a plane in the 3D world:
  _threePlaneTexture: THREE.Texture | null = null;
  _threePlaneGeometry: THREE.PlaneGeometry | null = null;
  _threePlaneMaterial: THREE.MeshBasicMaterial | null = null;
  _threePlaneMesh: THREE.Mesh | null = null;

  _showObjectInstancesIn3D: boolean;

  constructor({
    project,
    layout,
    layer,
    viewPosition,
    instances,
    onInstanceClicked,
    onInstanceRightClicked,
    onInstanceDoubleClicked,
    onOverInstance,
    onOutInstance,
    onMoveInstance,
    onMoveInstanceEnd,
    onDownInstance,
    pixiRenderer,
    showObjectInstancesIn3D,
  }: {
    project: gdProject,
    instances: gdInitialInstancesContainer,
    layout: gdLayout,
    layer: gdLayer,
    viewPosition: ViewPosition,
    onInstanceClicked: gdInitialInstance => void,
    onInstanceRightClicked: ({|
      offsetX: number,
      offsetY: number,
      x: number,
      y: number,
    |}) => void,
    onInstanceDoubleClicked: gdInitialInstance => void,
    onOverInstance: gdInitialInstance => void,
    onOutInstance: gdInitialInstance => void,
    onMoveInstance: (gdInitialInstance, number, number) => void,
    onMoveInstanceEnd: void => void,
    onDownInstance: (gdInitialInstance, number, number) => void,
    pixiRenderer: PIXI.Renderer,
    showObjectInstancesIn3D: boolean,
  }) {
    this.project = project;
    this.instances = instances;
    this.layout = layout;
    this.layer = layer; // /!\ Don't store any other reference.
    // `layer` can be changed at any moment (see InstancesRenderer).
    this.viewPosition = viewPosition;
    this.onInstanceClicked = onInstanceClicked;
    this.onInstanceRightClicked = onInstanceRightClicked;
    this.onInstanceDoubleClicked = onInstanceDoubleClicked;
    this.onOverInstance = onOverInstance;
    this.onOutInstance = onOutInstance;
    this.onMoveInstance = onMoveInstance;
    this.onMoveInstanceEnd = onMoveInstanceEnd;
    this.onDownInstance = onDownInstance;

    this.viewTopLeft = [0, 0]; // Used for instances culling on rendering
    this.viewBottomRight = [0, 0]; // Used for instances culling on rendering

    this.pixiContainer = new PIXI.Container();

    this._showObjectInstancesIn3D = showObjectInstancesIn3D;

    // Functor used to render an instance
    this.instancesRenderer = new gd.InitialInstanceJSFunctor();
    // $FlowFixMe - invoke is not writable
    this.instancesRenderer.invoke = instancePtr => {
      // $FlowFixMe - wrapPointer is not exposed
      const instance: gdInitialInstance = gd.wrapPointer(
        instancePtr,
        gd.InitialInstance
      );

      //Get the "RenderedInstance" object associated to the instance and tell it to update.
      var renderedInstance:
        | RenderedInstance
        | Rendered3DInstance
        | null = this.getRendererOfInstance(instance);
      if (!renderedInstance) return;

      const pixiObject = renderedInstance.getPixiObject();
      if (pixiObject) pixiObject.zOrder = instance.getZOrder();

      // "Culling" improves rendering performance of large levels
      const isVisible = this._isInstanceVisible(instance);
      if (pixiObject) {
        pixiObject.visible = isVisible;
        pixiObject.interactive = !(
          this.layer.isLocked() ||
          (instance.isLocked() && instance.isSealed())
        );
      }
      if (isVisible) renderedInstance.update();

      if (renderedInstance instanceof Rendered3DInstance) {
        const threeObject = renderedInstance.getThreeObject();
        if (this._threeGroup && threeObject) {
          this._threeGroup.add(threeObject);
        }
      }

      renderedInstance.wasUsed = true;
    };

    // TODO (3D) Should it handle preference changes without needing to reopen tabs?
    if (this._showObjectInstancesIn3D) {
      this._setup3dRendering(pixiRenderer);
    }
  }

  getPixiContainer() {
    return this.pixiContainer;
  }

  getThreeScene(): THREE.Scene | null {
    return this._threeScene;
  }

  getThreeCamera(): THREE.PerspectiveCamera | null {
    return this._threeCamera;
  }

  getThreePlaneMesh(): THREE.Mesh | null {
    return this._threePlaneMesh;
  }

  getUnrotatedInstanceLeft = (instance: gdInitialInstance) => {
    return (
      instance.getX() -
      (this.renderedInstances[instance.ptr]
        ? this.renderedInstances[instance.ptr].getOriginX()
        : 0)
    );
  };

  getUnrotatedInstanceTop = (instance: gdInitialInstance) => {
    return (
      instance.getY() -
      (this.renderedInstances[instance.ptr]
        ? this.renderedInstances[instance.ptr].getOriginY()
        : 0)
    );
  };

  getUnrotatedInstanceSize = (instance: gdInitialInstance) => {
    const renderedInstance = this.renderedInstances[instance.ptr];
    const hasCustomSize = instance.hasCustomSize();
    const hasCustomDepth = instance.hasCustomDepth();
    const width = hasCustomSize
      ? instance.getCustomWidth()
      : renderedInstance
      ? renderedInstance.getDefaultWidth()
      : 0;
    const height = hasCustomSize
      ? instance.getCustomHeight()
      : renderedInstance
      ? renderedInstance.getDefaultHeight()
      : 0;
    const depth = hasCustomDepth
      ? instance.getCustomDepth()
      : renderedInstance
      ? renderedInstance.getDefaultDepth()
      : 0;

    return [width, height, depth];
  };

  getUnrotatedInstanceAABB(
    instance: gdInitialInstance,
    bounds: Rectangle
  ): Rectangle {
    const size = this.getUnrotatedInstanceSize(instance);
    const left = this.getUnrotatedInstanceLeft(instance);
    const top = this.getUnrotatedInstanceTop(instance);
    const right = left + size[0];
    const bottom = top + size[1];
    // TODO (3D): add support for zMin/zMax/depth.
    bounds.set({ left, top, right, bottom });
    return bounds;
  }

  _getInstanceRotatedRectangle(instance: gdInitialInstance): Polygon {
    const size = this.getUnrotatedInstanceSize(instance);
    const left = this.getUnrotatedInstanceLeft(instance);
    const top = this.getUnrotatedInstanceTop(instance);
    const right = left + size[0];
    const bottom = top + size[1];

    const rectangle = this._temporaryRectanglePath;

    rectangle[0][0] = left;
    rectangle[0][1] = top;

    rectangle[1][0] = left;
    rectangle[1][1] = bottom;

    rectangle[2][0] = right;
    rectangle[2][1] = bottom;

    rectangle[3][0] = right;
    rectangle[3][1] = top;

    let centerX = undefined;
    let centerY = undefined;

    if (this.renderedInstances[instance.ptr]) {
      centerX = left + this.renderedInstances[instance.ptr].getCenterX();
      centerY = top + this.renderedInstances[instance.ptr].getCenterY();
    }

    if (centerX === undefined || centerY === undefined) {
      centerX = (rectangle[0][0] + rectangle[2][0]) / 2;
      centerY = (rectangle[0][1] + rectangle[2][1]) / 2;
    }

    const angle = (instance.getAngle() * Math.PI) / 180;
    rotatePolygon(rectangle, centerX, centerY, angle);
    return rectangle;
  }

  getInstanceAABB(instance: gdInitialInstance, bounds: Rectangle): Rectangle {
    const angle = (instance.getAngle() * Math.PI) / 180;
    if (angle === 0) {
      return this.getUnrotatedInstanceAABB(instance, bounds);
    }

    const rotatedRectangle = this._getInstanceRotatedRectangle(instance);

    let left = Number.MAX_VALUE;
    let right = -Number.MAX_VALUE;
    let top = Number.MAX_VALUE;
    let bottom = -Number.MAX_VALUE;
    for (let i = 0, len = rotatedRectangle.length; i < len; ++i) {
      left = Math.min(left, rotatedRectangle[i][0]);
      right = Math.max(right, rotatedRectangle[i][0]);
      top = Math.min(top, rotatedRectangle[i][1]);
      bottom = Math.max(bottom, rotatedRectangle[i][1]);
    }
    // TODO (3D): add support for zMin/zMax/depth.
    bounds.set({ left, top, right, bottom });
    return bounds;
  }

  getRendererOfInstance = (instance: gdInitialInstance) => {
    var renderedInstance = this.renderedInstances[instance.ptr];
    if (renderedInstance === undefined) {
      //No renderer associated yet, the instance must have been just created!...
      const associatedObjectName = instance.getObjectName();
      const associatedObject = getObjectByName(
        this.project,
        this.layout,
        associatedObjectName
      );
      if (!associatedObject) return null;

      //...so let's create a renderer.
      renderedInstance = this.renderedInstances[
        instance.ptr
      ] = ObjectsRenderingService.createNewInstanceRenderer(
        this.project,
        this.layout,
        instance,
        associatedObject.getConfiguration(),
        this.pixiContainer,
        this._threeGroup
      );

      renderedInstance._pixiObject.interactive = true;
      gesture.panable(renderedInstance._pixiObject);
      makeDoubleClickable(renderedInstance._pixiObject);
      renderedInstance._pixiObject.on('click', event => {
        if (event.data.originalEvent.button === 0)
          this.onInstanceClicked(instance);
      });
      renderedInstance._pixiObject.on('doubleclick', () => {
        this.onInstanceDoubleClicked(instance);
      });
      renderedInstance._pixiObject.on('mouseover', () => {
        this.onOverInstance(instance);
      });
      renderedInstance._pixiObject.on(
        'mousedown',
        (event: PIXI.InteractionEvent) => {
          if (event.data.originalEvent.button === 0) {
            const viewPoint = event.data.global;
            const scenePoint = this.viewPosition.toSceneCoordinates(
              viewPoint.x,
              viewPoint.y
            );
            this.onDownInstance(instance, scenePoint[0], scenePoint[1]);
          }
        }
      );
      renderedInstance._pixiObject.on('rightclick', interactionEvent => {
        const {
          data: { global: viewPoint, originalEvent: event },
        } = interactionEvent;

        // First select the instance
        const scenePoint = this.viewPosition.toSceneCoordinates(
          viewPoint.x,
          viewPoint.y
        );
        this.onDownInstance(instance, scenePoint[0], scenePoint[1]);

        // Then call right click callback
        if (this.onInstanceRightClicked) {
          this.onInstanceRightClicked({
            offsetX: event.offsetX,
            offsetY: event.offsetY,
            x: event.clientX,
            y: event.clientY,
          });
        }

        return false;
      });
      renderedInstance._pixiObject.on('touchstart', event => {
        if (shouldBeHandledByPinch(event.data && event.data.originalEvent)) {
          return null;
        }

        const viewPoint = event.data.global;
        const scenePoint = this.viewPosition.toSceneCoordinates(
          viewPoint.x,
          viewPoint.y
        );
        this.onDownInstance(instance, scenePoint[0], scenePoint[1]);
      });
      renderedInstance._pixiObject.on('mouseout', () => {
        this.onOutInstance(instance);
      });
      renderedInstance._pixiObject.on('panmove', event => {
        if (shouldBeHandledByPinch(event.data && event.data.originalEvent)) {
          return null;
        }

        this.onMoveInstance(instance, event.deltaX, event.deltaY);
      });
      renderedInstance._pixiObject.on('panend', event => {
        this.onMoveInstanceEnd();
      });
    }

    return renderedInstance;
  };

  /**
   * This returns true if an instance is visible according to the viewPosition.
   * The approach is a naive bounding box testing but save rendering time on large
   * levels (though this could be improved with spatial partitioning).
   */
  _isInstanceVisible(instance: gdInitialInstance) {
    const aabb = this.getInstanceAABB(instance, this._temporaryRectangle);
    if (
      aabb.left + aabb.width() < this.viewTopLeft[0] ||
      aabb.top + aabb.height() < this.viewTopLeft[1] ||
      aabb.left > this.viewBottomRight[0] ||
      aabb.top > this.viewBottomRight[1]
    )
      return false;

    return true;
  }

  _computeViewBounds() {
    // Add a margin of 100 pixels around the view. Culling will hide PIXI objects,
    // and hidden objects won't respond to events. Hence, a margin allow the cursor to go
    // slightly out of the canvas when moving an instance, and still have the instance
    // to follow the cursor.
    const margin = 100;
    this.viewTopLeft = this.viewPosition.toSceneCoordinates(-margin, -margin);
    this.viewBottomRight = this.viewPosition.toSceneCoordinates(
      this.viewPosition.getWidth() + margin,
      this.viewPosition.getHeight() + margin
    );
  }

  render() {
    this._computeViewBounds();
    this.instances.iterateOverInstancesWithZOrdering(
      // $FlowFixMe - gd.castObject is not supporting typings.
      this.instancesRenderer,
      this.layer.getName()
    );
    this._updatePixiObjectsZOrder();
    this._updateVisibility();
    this._destroyUnusedInstanceRenderers();
  }

  /**
   * Create Three.js objects for 3D rendering of this layer.
   */
  _setup3dRendering(pixiRenderer: PIXI.Renderer): void {
    if (this._threeScene || this._threeGroup || this._threeCamera) {
      throw new Error(
        'Tried to setup 3D rendering for a layer that is already set up.'
      );
    }

    const threeScene = new THREE.Scene();
    this._threeScene = threeScene;

    // Use a mirroring on the Y axis to follow the same axis as in the 2D, PixiJS, rendering.
    // We use a mirroring rather than a camera rotation so that the Z order is not changed.
    threeScene.scale.y = -1;

    this._threeGroup = new THREE.Group();
    threeScene.add(this._threeGroup);

    const threeCamera = new THREE.PerspectiveCamera(45, 1, 0.1, 2000);
    threeCamera.rotation.order = 'ZYX';
    this._threeCamera = threeCamera;

    if (
      this._renderTexture ||
      this._threePlaneGeometry ||
      this._threePlaneMaterial ||
      this._threePlaneTexture ||
      this._threePlaneMesh
    ) {
      throw new Error(
        'Tried to setup PixiJS plane for 2D rendering in 3D for a layer that is already set up.'
      );
    }

    // If we have both 2D and 3D objects to be rendered, create a render texture that PixiJS will use
    // to render, and that will be projected on a plane by Three.js
    this._createPixiRenderTexture(pixiRenderer);

    // Create the texture to project on the plane.
    // Use a buffer to create a "fake" DataTexture, just so the texture
    // is considered initialized by Three.js.
    const width = 1;
    const height = 1;
    const size = width * height;
    const data = new Uint8Array(4 * size);
    const threePlaneTexture = new THREE.DataTexture(data, width, height);
    threePlaneTexture.needsUpdate = true;
    this._threePlaneTexture = threePlaneTexture;

    threePlaneTexture.generateMipmaps = false;
    const filter =
      this.project.getScaleMode() === 'nearest'
        ? THREE.NearestFilter
        : THREE.LinearFilter;
    threePlaneTexture.minFilter = filter;
    threePlaneTexture.magFilter = filter;
    threePlaneTexture.wrapS = THREE.ClampToEdgeWrapping;
    threePlaneTexture.wrapT = THREE.ClampToEdgeWrapping;

    // Create the plane that will show this texture.
    const threePlaneGeometry = new THREE.PlaneGeometry(1, 1);
    this._threePlaneGeometry = threePlaneGeometry;
    // This disable the gamma correction done by THREE as PIXI is already doing it.
    const noGammaCorrectionShader: THREE.ShaderMaterialParameters = {
      vertexShader: `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
      `,
      fragmentShader: `
        uniform sampler2D map;
        varying vec2 vUv;
        void main() {
          vec4 texel = texture2D(map, vUv);
          gl_FragColor = texel;
        }
      `,
      uniforms: {
        map: { value: this._threePlaneTexture },
      },
      side: THREE.FrontSide,
      transparent: true,
    };
    const threePlaneMaterial = new THREE.ShaderMaterial(
      noGammaCorrectionShader
    );
    this._threePlaneMaterial = threePlaneMaterial;

    // Finally, create the mesh shown in the scene.
    const threePlaneMesh = new THREE.Mesh(
      threePlaneGeometry,
      threePlaneMaterial
    );
    threeScene.add(threePlaneMesh);
    this._threePlaneMesh = threePlaneMesh;
  }

  /**
   * Create the PixiJS RenderTexture used to display the whole layer.
   * Can be used either for lighting or for rendering the layer in a texture
   * so it can then be consumed by Three.js to render it in 3D.
   */
  _createPixiRenderTexture(pixiRenderer: PIXI.Renderer | null): void {
    if (!pixiRenderer || pixiRenderer.type !== PIXI.RENDERER_TYPE.WEBGL) {
      return;
    }
    if (this._renderTexture) {
      console.error(
        'Tried to create a PixiJS RenderTexture for a layer that already has one.'
      );
      return;
    }

    this._oldWidth = pixiRenderer.screen.width;
    this._oldHeight = pixiRenderer.screen.height;
    const width = this._oldWidth;
    const height = this._oldHeight;
    const resolution = pixiRenderer.resolution;
    this._renderTexture = PIXI.RenderTexture.create({
      width,
      height,
      resolution,
    });
    this._renderTexture.baseTexture.scaleMode = PIXI.SCALE_MODES.LINEAR;
    console.info(`RenderTexture created for layer ${this.layer.getName()}.`);
  }

  /**
   * Render the layer of the PixiJS RenderTexture, so that it can be then be
   * consumed by Three.js (for 2D+3D layers).
   */
  renderOnPixiRenderTexture(pixiRenderer: PIXI.Renderer) {
    if (!this._renderTexture) {
      return;
    }
    if (
      this._oldWidth !== pixiRenderer.screen.width ||
      this._oldHeight !== pixiRenderer.screen.height
    ) {
      this._renderTexture.resize(
        pixiRenderer.screen.width,
        pixiRenderer.screen.height
      );
      this._oldWidth = pixiRenderer.screen.width;
      this._oldHeight = pixiRenderer.screen.height;
    }
    const oldRenderTexture = pixiRenderer.renderTexture.current;
    const oldSourceFrame = pixiRenderer.renderTexture.sourceFrame;
    pixiRenderer.renderTexture.bind(this._renderTexture);

    pixiRenderer.renderTexture.clear([0, 0, 0, 0]);

    pixiRenderer.render(this.pixiContainer, {
      renderTexture: this._renderTexture,
      clear: false,
    });
    pixiRenderer.renderTexture.bind(
      oldRenderTexture,
      oldSourceFrame,
      undefined
    );
  }

  /**
   * Set the texture of the 2D plane in the 3D world to be the same WebGL texture
   * as the PixiJS RenderTexture - so that the 2D rendering can be shown in the 3D world.
   */
  updateThreePlaneTextureFromPixiRenderTexture(
    threeRenderer: THREE.WebGLRenderer,
    pixiRenderer: PIXI.Renderer
  ): void {
    if (!this._threePlaneTexture || !this._renderTexture) {
      return;
    }

    const glTexture = this._renderTexture.baseTexture._glTextures[
      pixiRenderer.CONTEXT_UID
    ];
    if (glTexture) {
      // "Hack" into the Three.js renderer by getting the internal WebGL texture for the PixiJS plane,
      // and set it so that it's the same as the WebGL texture for the PixiJS RenderTexture.
      // This works because PixiJS and Three.js are using the same WebGL context.
      const texture = threeRenderer.properties.get(this._threePlaneTexture);
      texture.__webglTexture = glTexture.texture;
    }
  }

  _updatePixiObjectsZOrder() {
    this.pixiContainer.children.sort((a, b) => {
      a.zOrder = a.zOrder || 0;
      b.zOrder = b.zOrder || 0;
      return a.zOrder - b.zOrder;
    });
  }

  _updateVisibility() {
    const isVisible = this.layer.getVisibility();
    this.pixiContainer.visible = isVisible;
    if (this._threeScene) {
      this._threeScene.visible = isVisible;
    }
  }

  /**
   * Delete instance renderers of the specified objects, which will then be recreated during
   * the next render.
   * @param {string} objectName The name of the object for which instance must be re-rendered.
   */
  resetInstanceRenderersFor(objectName: string) {
    for (let s in this.renderedInstances) {
      let i = Number(s);
      if (this.renderedInstances.hasOwnProperty(i)) {
        const renderedInstance = this.renderedInstances[i];
        if (renderedInstance.getInstance().getObjectName() === objectName) {
          renderedInstance.onRemovedFromScene();
          delete this.renderedInstances[i];
        }
      }
    }
  }

  /**
   * Remove rendered instances that are not associated to any instance anymore
   * (this can happen after an instance has been deleted).
   */
  _destroyUnusedInstanceRenderers() {
    for (let s in this.renderedInstances) {
      let i = Number(s);
      if (this.renderedInstances.hasOwnProperty(i)) {
        const renderedInstance = this.renderedInstances[i];
        if (!renderedInstance.wasUsed) {
          renderedInstance.onRemovedFromScene();
          delete this.renderedInstances[i];
        } else renderedInstance.wasUsed = false;
      }
    }
  }

  delete() {
    // Destroy all instances
    for (let s in this.renderedInstances) {
      let i = Number(s);
      if (this.renderedInstances.hasOwnProperty(i)) {
        const renderedInstance = this.renderedInstances[i];
        renderedInstance.onRemovedFromScene(); // TODO: pass argument to tell it's even worth removing from container?
        delete this.renderedInstances[i];
      }
    }

    // Destroy the container
    this.pixiContainer.destroy();

    // Destroy the object iterating on instances
    this.instancesRenderer.delete();
  }
}
