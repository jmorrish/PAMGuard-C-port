    // ================= composable processing graph =======================

    let graphModuleTypes = [];
    let graphDraft = null;
    let graphRuntimeStatus = null;
    let graphDirty = false;
    let graphLayout = {
      schemaVersion: 1,
      positions: {},
      viewport: { x: 70, y: 55, zoom: 1 }
    };
    let graphUndoStack = [];
    let graphRedoStack = [];
    let graphSelectedNodeId = "";
    let graphSelectedConnectionId = "";
    let graphContextModuleId = "";
    let graphConnectDraft = null;
    let graphNodeDrag = null;
    let graphPanDrag = null;
    const dataModelCallbacks = {
      onApplied: async () => {},
      onAcquisitionListChanged: () => {}
    };
    const GRAPH_LAYOUT_STORAGE_KEY = "pamguard.module-graph-layout.v1";
    const GRAPH_NODE_WIDTH = 278;
    function graphType(typeId) {
      return graphModuleTypes.find((type) => type.id === typeId);
    }

    function graphSetValidation(message, valid = null) {
      const element = $("graphValidation");
      element.textContent = message;
      element.classList.toggle("ok", valid === true);
      element.classList.toggle("bad", valid === false);
    }

    function graphMarkDirty() {
      graphDirty = true;
      renderGraphMeta();
    }

    function graphClone(value) {
      return structuredClone(value);
    }

    function graphSnapshot() {
      return {
        modules: graphClone(graphDraft?.modules || []),
        connections: graphClone(graphDraft?.connections || []),
        layout: graphClone(graphLayout),
        dirty: graphDirty
      };
    }

    function graphRecordUndo() {
      if (!graphDraft) return;
      graphUndoStack.push(graphSnapshot());
      if (graphUndoStack.length > 60) graphUndoStack.shift();
      graphRedoStack = [];
      updateGraphUndoButtons();
    }

    function graphRestoreSnapshot(snapshot) {
      if (!graphDraft || !snapshot) return;
      graphDraft.modules = graphClone(snapshot.modules);
      graphDraft.connections = graphClone(snapshot.connections);
      graphLayout = graphClone(snapshot.layout);
      graphDirty = snapshot.dirty;
      saveGraphLayout();
      renderGraphEditor();
    }

    function graphUndo() {
      if (!graphUndoStack.length) return;
      graphRedoStack.push(graphSnapshot());
      graphRestoreSnapshot(graphUndoStack.pop());
      updateGraphUndoButtons();
    }

    function graphRedo() {
      if (!graphRedoStack.length) return;
      graphUndoStack.push(graphSnapshot());
      graphRestoreSnapshot(graphRedoStack.pop());
      updateGraphUndoButtons();
    }

    function updateGraphUndoButtons() {
      $("graphUndo").disabled = !graphUndoStack.length;
      $("graphRedo").disabled = !graphRedoStack.length;
    }

    function loadGraphLayout() {
      try {
        const saved = JSON.parse(localStorage.getItem(
          GRAPH_LAYOUT_STORAGE_KEY) || "null");
        if (saved?.schemaVersion === 1 &&
            saved.positions && saved.viewport) {
          graphLayout = saved;
        }
      }
      catch {
        localStorage.removeItem(GRAPH_LAYOUT_STORAGE_KEY);
      }
    }

    function saveGraphLayout() {
      localStorage.setItem(
        GRAPH_LAYOUT_STORAGE_KEY,
        JSON.stringify(graphLayout));
    }

    function graphDefaultPosition(index) {
      return {
        x: 80 + (index % 3) * 360,
        y: 70 + Math.floor(index / 3) * 190
      };
    }

    function ensureGraphPositions() {
      const activeIds = new Set(graphDraft?.modules.map(
        (module) => module.id) || []);
      for (const id of Object.keys(graphLayout.positions)) {
        if (!activeIds.has(id)) delete graphLayout.positions[id];
      }
      (graphDraft?.modules || []).forEach((module, index) => {
        if (!graphLayout.positions[module.id]) {
          graphLayout.positions[module.id] = graphDefaultPosition(index);
        }
      });
      saveGraphLayout();
    }

    function renderGraphMeta() {
      if (!graphDraft) return;
      const running = graphRuntimeStatus?.running ? "running" : "stopped";
      $("graphMeta").textContent =
        `revision ${graphDraft.revision} · ${graphDraft.modules.length} modules · ` +
        `${graphDraft.connections.length} connections · ${running}` +
        (graphDirty ? " · unapplied changes" : "");
    }

    function applyGraphViewport() {
      const viewport = graphLayout.viewport;
      $("graphWorld").style.transform =
        `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})`;
      $("graphZoomValue").textContent =
        `${Math.round(viewport.zoom * 100)}%`;
    }

    function graphScreenToWorld(clientX, clientY) {
      const rect = $("graphCanvas").getBoundingClientRect();
      const viewport = graphLayout.viewport;
      return {
        x: (clientX - rect.left - viewport.x) / viewport.zoom,
        y: (clientY - rect.top - viewport.y) / viewport.zoom
      };
    }

    function graphSetZoom(nextZoom, clientX = null, clientY = null) {
      const canvas = $("graphCanvas");
      const rect = canvas.getBoundingClientRect();
      const viewport = graphLayout.viewport;
      const oldZoom = viewport.zoom;
      const zoom = Math.min(1.8, Math.max(0.35, nextZoom));
      const anchorX = clientX ?? rect.left + rect.width / 2;
      const anchorY = clientY ?? rect.top + rect.height / 2;
      const worldX = (anchorX - rect.left - viewport.x) / oldZoom;
      const worldY = (anchorY - rect.top - viewport.y) / oldZoom;
      viewport.zoom = zoom;
      viewport.x = anchorX - rect.left - worldX * zoom;
      viewport.y = anchorY - rect.top - worldY * zoom;
      applyGraphViewport();
      saveGraphLayout();
    }

    function graphFitAll() {
      const modules = graphDraft?.modules || [];
      if (!modules.length) {
        graphLayout.viewport = { x: 70, y: 55, zoom: 1 };
        applyGraphViewport();
        saveGraphLayout();
        return;
      }
      const positions = modules.map(
        (module) => graphLayout.positions[module.id]);
      const minX = Math.min(...positions.map((position) => position.x));
      const minY = Math.min(...positions.map((position) => position.y));
      const maxX = Math.max(...positions.map(
        (position) => position.x + GRAPH_NODE_WIDTH));
      const maxY = Math.max(...positions.map(
        (position) => position.y + 165));
      const rect = $("graphCanvas").getBoundingClientRect();
      const zoom = Math.min(
        1.25,
        Math.max(0.35, Math.min(
          (rect.width - 90) / Math.max(1, maxX - minX),
          (rect.height - 90) / Math.max(1, maxY - minY))));
      graphLayout.viewport = {
        x: (rect.width - (maxX - minX) * zoom) / 2 - minX * zoom,
        y: (rect.height - (maxY - minY) * zoom) / 2 - minY * zoom,
        zoom
      };
      applyGraphViewport();
      saveGraphLayout();
    }

    function graphFocusModule(moduleId) {
      const position = graphLayout.positions[moduleId];
      if (!position) return;
      const rect = $("graphCanvas").getBoundingClientRect();
      const zoom = graphLayout.viewport.zoom;
      graphLayout.viewport.x =
        rect.width / 2 - (position.x + GRAPH_NODE_WIDTH / 2) * zoom;
      graphLayout.viewport.y =
        rect.height / 2 - (position.y + 80) * zoom;
      graphSelectedNodeId = moduleId;
      graphSelectedConnectionId = "";
      applyGraphViewport();
      saveGraphLayout();
      renderGraphSelection();
      $("graphCanvas").focus();
    }

    function graphAutoLayout() {
      if (!graphDraft?.modules.length) return;
      graphRecordUndo();
      const modules = graphDraft.modules;
      const incoming = new Map(modules.map(
        (module) => [module.id, []]));
      const outgoing = new Map(modules.map(
        (module) => [module.id, []]));
      for (const connection of graphDraft.connections) {
        incoming.get(connection.target.moduleId)?.push(
          connection.source.moduleId);
        outgoing.get(connection.source.moduleId)?.push(
          connection.target.moduleId);
      }
      const indegree = new Map(modules.map(
        (module) => [module.id, incoming.get(module.id)?.length || 0]));
      const levels = new Map();
      const queue = modules.filter(
        (module) => indegree.get(module.id) === 0);
      queue.forEach((module) => levels.set(module.id, 0));
      while (queue.length) {
        const module = queue.shift();
        for (const childId of outgoing.get(module.id) || []) {
          const next = Math.max(
            levels.get(childId) || 0,
            (levels.get(module.id) || 0) + 1);
          levels.set(childId, next);
          indegree.set(childId, (indegree.get(childId) || 0) - 1);
          if (indegree.get(childId) === 0) {
            const child = modules.find(
              (candidate) => candidate.id === childId);
            if (child) queue.push(child);
          }
        }
      }
      modules.forEach((module) => {
        if (!levels.has(module.id)) levels.set(module.id, 0);
      });
      const columns = new Map();
      for (const module of modules) {
        const level = levels.get(module.id);
        if (!columns.has(level)) columns.set(level, []);
        columns.get(level).push(module);
      }
      for (const [level, column] of columns) {
        column.forEach((module, row) => {
          graphLayout.positions[module.id] = {
            x: 80 + level * 365,
            y: 65 + row * 190
          };
        });
      }
      saveGraphLayout();
      renderGraphEditor();
      requestAnimationFrame(graphFitAll);
    }

    function graphOutputCandidates(targetModule, inputPort) {
      const candidates = [];
      for (const module of graphDraft.modules) {
        const descriptor = graphType(module.typeId);
        if (!descriptor) continue;
        for (const port of descriptor.ports.filter(
          (candidate) =>
            candidate.direction === "output" &&
            candidate.dataType === inputPort.dataType)) {
          const requiredCapabilities = inputPort.capabilities || [];
          const providedCapabilities = port.capabilities || [];
          if (requiredCapabilities.every(
              (capability) => providedCapabilities.includes(capability))) {
            candidates.push({ module, port });
          }
        }
      }
      return candidates;
    }

    function graphPortsCompatible(sourceModuleId, sourcePort, targetModuleId,
                                  targetPort) {
      if (!sourcePort || !targetPort ||
          sourceModuleId === targetModuleId ||
          sourcePort.direction !== "output" ||
          targetPort.direction !== "input" ||
          sourcePort.dataType !== targetPort.dataType) {
        return false;
      }
      const required = targetPort.capabilities || [];
      const provided = sourcePort.capabilities || [];
      return required.every(
        (capability) => provided.includes(capability));
    }

    function graphModuleIcon(descriptor) {
      return (descriptor?.name || "?")
        .split(/\s+/)
        .slice(0, 2)
        .map((part) => part[0])
        .join("")
        .toUpperCase();
    }

    function renderGraphPalette() {
      const query = $("graphModuleSearch").value.trim().toLowerCase();
      const grouped = new Map();
      for (const descriptor of graphModuleTypes
        .slice()
        .sort((left, right) =>
          `${left.category}:${left.name}`.localeCompare(
            `${right.category}:${right.name}`))) {
        const searchText =
          `${descriptor.name} ${descriptor.category} ${descriptor.description}`.toLowerCase();
        if (query && !searchText.includes(query)) continue;
        if (!grouped.has(descriptor.category)) {
          grouped.set(descriptor.category, []);
        }
        grouped.get(descriptor.category).push(descriptor);
      }
      const palette = $("graphPalette");
      palette.replaceChildren();
      for (const [category, descriptors] of grouped) {
        const group = document.createElement("details");
        group.className = "graph-palette-category";
        group.open = true;
        const summary = document.createElement("summary");
        summary.textContent = `${category} (${descriptors.length})`;
        const items = document.createElement("div");
        items.className = "graph-palette-items";
        for (const descriptor of descriptors) {
          const item = document.createElement("button");
          item.className = "graph-palette-item";
          item.draggable = true;
          item.dataset.typeId = descriptor.id;
          item.title = `${descriptor.description}\nDrag to add`;
          const icon = document.createElement("span");
          icon.className = "graph-module-icon";
          icon.textContent = graphModuleIcon(descriptor);
          const text = document.createElement("span");
          const name = document.createElement("strong");
          name.textContent = descriptor.name;
          const description = document.createElement("small");
          description.textContent = descriptor.description;
          text.append(name, description);
          item.append(icon, text);
          item.addEventListener("dragstart", (event) => {
            event.dataTransfer.effectAllowed = "copy";
            event.dataTransfer.setData(
              "application/x-pamguard-module",
              descriptor.id);
            event.dataTransfer.setData("text/plain", descriptor.id);
          });
          item.addEventListener("dblclick", () => {
            const rect = $("graphCanvas").getBoundingClientRect();
            graphAddModuleAt(
              descriptor.id,
              graphScreenToWorld(
                rect.left + rect.width / 2,
                rect.top + rect.height / 2));
          });
          items.append(item);
        }
        group.append(summary, items);
        palette.append(group);
      }
      if (!palette.childElementCount) {
        const empty = document.createElement("div");
        empty.className = "operator-empty";
        empty.textContent = "No modules match this search.";
        palette.append(empty);
      }
    }

    function graphAddModuleAt(typeId, position) {
      if (!graphDraft) return;
      const descriptor = graphType(typeId);
      if (!descriptor) return;
      const instances = graphDraft.modules.filter(
        (module) => module.typeId === descriptor.id).length;
      if (descriptor.maximumInstances !== null &&
          instances >= descriptor.maximumInstances) {
        graphSetValidation(
          `${descriptor.name} permits at most ${descriptor.maximumInstances} instance(s).`,
          false);
        return;
      }
      graphRecordUndo();
      const id = graphNewModuleId(descriptor.id);
      graphDraft.modules.push({
        id,
        typeId: descriptor.id,
        name: descriptor.name,
        enabled: true,
        settings: graphClone(descriptor.defaultSettings || {})
      });
      graphLayout.positions[id] = {
        x: Math.max(20, Math.min(3650, position.x - GRAPH_NODE_WIDTH / 2)),
        y: Math.max(20, Math.min(2400, position.y - 35))
      };
      graphSelectedNodeId = id;
      graphSelectedConnectionId = "";
      graphMarkDirty();
      saveGraphLayout();
      renderGraphEditor();
    }

    function graphRemoveModule(moduleId, ask = true) {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === moduleId);
      if (!module) return;
      if (ask && !confirm(
        `Remove '${module.name}' and all of its connections from the draft graph?`)) {
        return;
      }
      graphRecordUndo();
      graphDraft.modules = graphDraft.modules.filter(
        (candidate) => candidate.id !== moduleId);
      graphDraft.connections = graphDraft.connections.filter(
        (connection) =>
          connection.source.moduleId !== moduleId &&
          connection.target.moduleId !== moduleId);
      delete graphLayout.positions[moduleId];
      if (graphSelectedNodeId === moduleId) graphSelectedNodeId = "";
      graphMarkDirty();
      saveGraphLayout();
      renderGraphEditor();
    }

    function graphDuplicateModule(moduleId) {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === moduleId);
      const descriptor = module ? graphType(module.typeId) : null;
      if (!module || !descriptor) return;
      const instances = graphDraft.modules.filter(
        (candidate) => candidate.typeId === descriptor.id).length;
      if (descriptor.maximumInstances !== null &&
          instances >= descriptor.maximumInstances) {
        graphSetValidation(
          `${descriptor.name} permits at most ${descriptor.maximumInstances} instance(s).`,
          false);
        return;
      }
      graphRecordUndo();
      const copy = graphClone(module);
      copy.id = graphNewModuleId(module.typeId);
      copy.name = `${module.name} copy`;
      graphDraft.modules.push(copy);
      const position = graphLayout.positions[moduleId] || { x: 80, y: 70 };
      graphLayout.positions[copy.id] = {
        x: position.x + 42,
        y: position.y + 42
      };
      graphSelectedNodeId = copy.id;
      graphMarkDirty();
      saveGraphLayout();
      renderGraphEditor();
    }

    function graphPortElement(module, port, connected) {
      const socket = document.createElement("span");
      socket.className =
        `graph-port ${port.direction}${connected ? " connected" : ""}`;
      socket.dataset.moduleId = module.id;
      socket.dataset.portId = port.id;
      socket.dataset.direction = port.direction;
      socket.title =
        `${port.name}\n${port.dataType}` +
        ((port.capabilities || []).length
          ? `\n${port.capabilities.join(", ")}`
          : "");
      if (port.direction === "output") {
        socket.addEventListener("pointerdown", (event) => {
          if (event.button !== 0) return;
          event.preventDefault();
          event.stopPropagation();
          graphConnectDraft = {
            source: { moduleId: module.id, portId: port.id },
            sourcePort: port,
            pointer: graphScreenToWorld(event.clientX, event.clientY)
          };
          $("graphCanvas").classList.add("connecting");
          for (const input of document.querySelectorAll(
            ".graph-port.input")) {
            const targetModule = graphDraft.modules.find(
              (candidate) => candidate.id === input.dataset.moduleId);
            const targetDescriptor = graphType(targetModule?.typeId);
            const targetPort = targetDescriptor?.ports.find(
              (candidate) =>
                candidate.direction === "input" &&
                candidate.id === input.dataset.portId);
            input.classList.add(
              graphPortsCompatible(
                module.id,
                port,
                targetModule?.id,
                targetPort)
                ? "compatible"
                : "incompatible");
          }
          renderGraphConnections();
        });
      }
      return socket;
    }

    function graphStartNodeDrag(event, moduleId) {
      if (event.button !== 0) return;
      event.preventDefault();
      event.stopPropagation();
      hideGraphContextMenu();
      graphRecordUndo();
      graphSelectedNodeId = moduleId;
      graphSelectedConnectionId = "";
      const start = graphScreenToWorld(event.clientX, event.clientY);
      const position = graphLayout.positions[moduleId];
      graphNodeDrag = {
        moduleId,
        start,
        origin: { x: position.x, y: position.y }
      };
      renderGraphSelection();
    }

    function renderGraphNode(module) {
      const descriptor = graphType(module.typeId);
      const runtimeModule = graphRuntimeStatus?.modules?.find(
        (candidate) => candidate.moduleId === module.id);
      const node = document.createElement("article");
      node.className =
        `graph-node${module.id === graphSelectedNodeId ? " selected" : ""}` +
        `${module.enabled === false ? " disabled" : ""}`;
      node.dataset.moduleId = module.id;
      const position = graphLayout.positions[module.id];
      node.style.left = `${position.x}px`;
      node.style.top = `${position.y}px`;
      node.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        graphSelectedNodeId = module.id;
        graphSelectedConnectionId = "";
        renderGraphSelection();
        showGraphContextMenu(module.id, event.clientX, event.clientY);
      });
      node.addEventListener("pointerdown", (event) => {
        if (event.target.closest("button, .graph-port")) return;
        graphSelectedNodeId = module.id;
        graphSelectedConnectionId = "";
        hideGraphContextMenu();
        renderGraphSelection();
      });

      const head = document.createElement("header");
      head.className = "graph-node-head";
      head.addEventListener(
        "pointerdown",
        (event) => graphStartNodeDrag(event, module.id));
      head.addEventListener("dblclick", () => openGraphSettings(module.id));
      const icon = document.createElement("span");
      icon.className = "graph-module-icon";
      icon.textContent = graphModuleIcon(descriptor);
      const title = document.createElement("span");
      title.className = "graph-node-title";
      const titleName = document.createElement("strong");
      titleName.textContent = module.name;
      const typeName = document.createElement("small");
      typeName.textContent =
        descriptor?.name || `Unavailable · ${module.typeId}`;
      title.append(titleName, typeName);
      const status = document.createElement("span");
      status.className = "graph-node-status";
      status.title = runtimeModule?.state || "draft";
      if (runtimeModule?.state === "running") status.classList.add("running");
      if (runtimeModule?.state === "error") status.classList.add("error");
      head.append(icon, title, status);

      const body = document.createElement("div");
      body.className = "graph-node-body";
      if (descriptor?.description) {
        const description = document.createElement("div");
        description.className = "graph-node-description";
        description.textContent = descriptor.description;
        body.append(description);
      }
      if (module.typeId === "pamguard.acquisition") {
        const capture = captureStatusByModule.get(module.id);
        const captureState = document.createElement("div");
        captureState.className =
          `graph-capture-state${capture ? " running" : ""}`;
        captureState.textContent = capture
          ? `Capture running · ${capture.kind} · pid ${capture.pid}`
          : "Capture stopped";
        captureState.title = capture
          ? `${capture.source} · graph revision ${capture.graphRevision}`
          : "No capture process is registered for this acquisition.";
        body.append(captureState);
      }
      const inputs = (descriptor?.ports || []).filter(
        (port) => port.direction === "input");
      const outputs = (descriptor?.ports || []).filter(
        (port) => port.direction === "output");
      const rows = Math.max(1, inputs.length, outputs.length);
      for (let index = 0; index < rows; index++) {
        const row = document.createElement("div");
        row.className = "graph-port-row";
        const inputWrap = document.createElement("span");
        inputWrap.className = "graph-port-wrap input";
        const input = inputs[index];
        if (input) {
          const connected = graphDraft.connections.some(
            (connection) =>
              connection.target.moduleId === module.id &&
              connection.target.portId === input.id);
          const socket = graphPortElement(module, input, connected);
          const label = document.createElement("span");
          label.className = "graph-port-label";
          label.textContent =
            `${input.name}${input.required ? " *" : ""}`;
          inputWrap.append(socket, label);
        }
        const outputWrap = document.createElement("span");
        outputWrap.className = "graph-port-wrap output";
        const output = outputs[index];
        if (output) {
          const connected = graphDraft.connections.some(
            (connection) =>
              connection.source.moduleId === module.id &&
              connection.source.portId === output.id);
          const label = document.createElement("span");
          label.className = "graph-port-label";
          label.textContent = output.name;
          outputWrap.append(
            label,
            graphPortElement(module, output, connected));
        }
        row.append(inputWrap, outputWrap);
        body.append(row);
      }

      const foot = document.createElement("footer");
      foot.className = "graph-node-foot";
      const parity = document.createElement("span");
      parity.textContent =
        `${descriptor?.category || "Unavailable"} · ` +
        `${descriptor?.parityStatus || "unknown"}`;
      const configure = document.createElement("button");
      configure.className = "secondary";
      configure.textContent = "Configure";
      configure.addEventListener("click", () => openGraphSettings(module.id));
      foot.append(parity, configure);
      node.append(head, body, foot);
      return node;
    }

    function graphSocketPoint(moduleId, portId, direction) {
      const selector =
        `.graph-port.${direction}[data-module-id="${CSS.escape(moduleId)}"]` +
        `[data-port-id="${CSS.escape(portId)}"]`;
      const socket = document.querySelector(selector);
      if (!socket) return null;
      const rect = socket.getBoundingClientRect();
      return graphScreenToWorld(
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
    }

    function graphConnectionPath(start, end) {
      const distance = Math.max(70, Math.abs(end.x - start.x) * 0.48);
      return `M ${start.x} ${start.y} C ${start.x + distance} ${start.y}, ` +
        `${end.x - distance} ${end.y}, ${end.x} ${end.y}`;
    }

    function graphSvgPath(pathData, className, connectionId = "") {
      const path = document.createElementNS(
        "http://www.w3.org/2000/svg",
        "path");
      path.setAttribute("d", pathData);
      path.setAttribute("class", className);
      if (connectionId) path.dataset.connectionId = connectionId;
      return path;
    }

    function renderGraphConnections() {
      const svg = $("graphWires");
      svg.replaceChildren();
      for (const connection of graphDraft?.connections || []) {
        const start = graphSocketPoint(
          connection.source.moduleId,
          connection.source.portId,
          "output");
        const end = graphSocketPoint(
          connection.target.moduleId,
          connection.target.portId,
          "input");
        if (!start || !end) continue;
        const pathData = graphConnectionPath(start, end);
        const targetModule = graphDraft.modules.find(
          (module) => module.id === connection.target.moduleId);
        const targetCategory = graphType(targetModule?.typeId)?.category || "";
        const connectionRole = targetCategory === "Displays"
          ? " display"
          : ["Output", "Storage"].includes(targetCategory)
            ? " output"
            : "";
        const hit = graphSvgPath(
          pathData,
          "graph-connection hit",
          connection.id);
        hit.addEventListener("pointerdown", (event) => {
          event.stopPropagation();
          graphSelectedConnectionId = connection.id;
          graphSelectedNodeId = "";
          renderGraphSelection();
        });
        hit.addEventListener("contextmenu", (event) => {
          event.preventDefault();
          graphSelectedConnectionId = connection.id;
          graphSelectedNodeId = "";
          if (confirm("Disconnect this data-block connection from the draft graph?")) {
            graphRecordUndo();
            graphDraft.connections = graphDraft.connections.filter(
              (candidate) => candidate.id !== connection.id);
            graphMarkDirty();
            renderGraphEditor();
          }
        });
        const visible = graphSvgPath(
          pathData,
          `graph-connection${connectionRole}` +
            `${connection.id === graphSelectedConnectionId ? " selected" : ""}`,
          connection.id);
        svg.append(hit, visible);
      }
      if (graphConnectDraft) {
        const start = graphSocketPoint(
          graphConnectDraft.source.moduleId,
          graphConnectDraft.source.portId,
          "output");
        if (start) {
          svg.append(graphSvgPath(
            graphConnectionPath(start, graphConnectDraft.pointer),
            "graph-connection preview"));
        }
      }
    }

    function renderGraphSelection() {
      for (const node of document.querySelectorAll(".graph-node")) {
        node.classList.toggle(
          "selected",
          node.dataset.moduleId === graphSelectedNodeId);
      }
      for (const path of document.querySelectorAll(
        ".graph-connection:not(.hit)")) {
        path.classList.toggle(
          "selected",
          path.dataset.connectionId === graphSelectedConnectionId);
      }
    }

    function updateGraphOperatorInputs() {
      const operatorSelect = $("graphOperatorInput");
      const priorOperator = operatorSelect.value;
      const operatorModules = graphDraft.modules.filter((module) =>
        ["pamguard.effort-monitor",
         "pamguard.aural-listening",
         "pamguard.user-input"].includes(module.typeId) &&
        module.enabled !== false);
      operatorSelect.replaceChildren(...operatorModules.map(
        (module) => new Option(module.name, module.id)));
      if (operatorModules.some(
          (module) => module.id === priorOperator)) {
        operatorSelect.value = priorOperator;
      }
      $("graphOperatorMeta").textContent = operatorModules.length
        ? `${operatorModules.length} operator input module${operatorModules.length === 1 ? "" : "s"} available`
        : "Add and apply an Effort, Aural Listening, or User Input module.";
    }

    function renderGraphEditor() {
      if (!graphDraft) return;
      ensureGraphPositions();
      const container = $("graphNodes");
      container.replaceChildren(
        ...graphDraft.modules.map(renderGraphNode));
      $("graphCanvasEmpty").style.display =
        graphDraft.modules.length ? "none" : "grid";
      updateGraphOperatorInputs();
      dataModelCallbacks.onAcquisitionListChanged(
        graphDraft.modules.filter(
          (module) =>
            module.typeId === "pamguard.acquisition" &&
            module.enabled !== false));
      renderGraphMeta();
      applyGraphViewport();
      updateGraphUndoButtons();
      requestAnimationFrame(renderGraphConnections);
    }

    function graphNewModuleId(typeId) {
      const prefix = typeId.replace(/^pamguard\./, "").replace(
        /[^a-z0-9]+/gi,
        "-");
      let id;
      do {
        id = `${prefix}-${crypto.randomUUID().slice(0, 8)}`;
      } while (graphDraft.modules.some((module) => module.id === id));
      return id;
    }

    async function loadGraphEditor() {
      try {
        const [catalogue, graph, runtime, captureStatus] =
          await Promise.all([
          requestJson(api("/module-types")),
          requestJson(api("/module-graph")),
          requestJson(api("/module-runtime/status")),
          requestJson(api("/capture/status"))
        ]);
        graphModuleTypes = Array.isArray(catalogue.moduleTypes)
          ? catalogue.moduleTypes
          : [];
        graphDraft = graph;
        graphRuntimeStatus = runtime;
        applyCaptureStatus(captureStatus);
        graphDirty = false;
        graphUndoStack = [];
        graphRedoStack = [];
        graphSelectedNodeId = "";
        graphSelectedConnectionId = "";
        renderGraphPalette();
        renderGraphEditor();
        graphSetValidation(
          `Loaded authoritative graph revision ${graph.revision}.`,
          true);
      }
      catch (error) {
        graphSetValidation(`Graph load failed: ${error}`, false);
      }
    }

    function graphRequestDocument() {
      return {
        schemaVersion: graphDraft.schemaVersion || 1,
        revision: graphDraft.revision || 0,
        acquisition: graphDraft.acquisition || {},
        clock: graphDraft.clock || {},
        persistence: graphDraft.persistence || {},
        modules: graphDraft.modules,
        connections: graphDraft.connections
      };
    }

    async function validateGraphEditor() {
      try {
        const result = await requestJson(api("/module-graph/validate"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(graphRequestDocument())
        });
        if (result.valid) {
          graphSetValidation(
            "Valid graph. It can be applied without disrupting the current runtime.",
            true);
        }
        else {
          graphSetValidation((result.issues || []).map(
            (issue) => `${issue.code}: ${issue.message}`).join("\n"),
            false);
        }
        return result.valid;
      }
      catch (error) {
        graphSetValidation(`Validation failed: ${error}`, false);
        return false;
      }
    }

    async function applyGraphEditor() {
      if (!await validateGraphEditor()) return;
      try {
        const document = graphRequestDocument();
        const applied = await requestJson(api("/module-graph"), {
          method: "PUT",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            ...document,
            expectedRevision: graphDraft.revision
          })
        });
        graphSetValidation(
          `Applied graph revision ${applied.revision}; runtime and persisted graph agree.`,
          true);
        await loadGraphEditor();
        await dataModelCallbacks.onApplied(
          structuredClone(graphDraft));
      }
      catch (error) {
        graphSetValidation(`Apply failed; live graph was not changed: ${error}`, false);
      }
    }

    async function controlGraphRuntime(action, restart = false) {
      try {
        graphRuntimeStatus = await requestJson(api("/module-runtime/control"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ action, restart })
        });
        renderGraphMeta();
        graphSetValidation(
          `${action} completed at graph revision ${graphRuntimeStatus.graphRevision}.`,
          true);
        await dataModelCallbacks.onApplied(
          structuredClone(graphDraft));
        await refreshCaptureStatus();
      }
      catch (error) {
        graphSetValidation(`${action} failed: ${error}`, false);
      }
    }

    function openGraphInspector(moduleId) {
      hideGraphContextMenu();
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === moduleId);
      const descriptor = module ? graphType(module.typeId) : null;
      const runtime = graphRuntimeStatus?.modules?.find(
        (candidate) => candidate.moduleId === moduleId);
      if (!module || !descriptor) return;
      $("graphInspectorTitle").textContent = `${module.name} data blocks`;
      $("graphInspectorSubtitle").textContent =
        `${descriptor.name} · ${runtime?.state || "draft/not applied"}`;
      const content = $("graphInspectorContent");
      content.replaceChildren();
      const ports = descriptor.ports || [];
      for (const port of ports) {
        const block = runtime?.outputs?.find(
          (candidate) => candidate.producerPortId === port.id);
        const card = document.createElement("div");
        card.className = "graph-inspector-card";
        const name = document.createElement("strong");
        name.textContent =
          `${port.direction === "input" ? "Input" : "Output"} · ${port.name}`;
        const type = document.createElement("code");
        type.textContent = port.dataType;
        const details = document.createElement("span");
        if (port.direction === "input") {
          const connection = graphDraft.connections.find(
            (candidate) =>
              candidate.target.moduleId === module.id &&
              candidate.target.portId === port.id);
          details.textContent = connection
            ? `Connected from ${connection.source.moduleId}:${connection.source.portId}`
            : port.required ? "Required · disconnected" : "Optional · disconnected";
        }
        else if (block) {
          details.textContent =
            `${block.id} · ${block.stats?.published || 0} published · ` +
            `${block.stats?.subscriberCount || 0} subscribers · ` +
            `${block.stats?.dropped || 0} dropped`;
        }
        else {
          details.textContent = "Output block will exist after the graph is applied.";
        }
        card.append(name, type, details);
        content.append(card);
      }
      $("graphInspectorDialog").showModal();
    }

    function showGraphContextMenu(moduleId, clientX, clientY) {
      graphContextModuleId = moduleId;
      const module = graphDraft.modules.find(
        (candidate) => candidate.id === moduleId);
      const menu = $("graphContextMenu");
      menu.querySelector('[data-graph-action="toggle"]').textContent =
        module?.enabled === false ? "Enable" : "Disable";
      menu.classList.add("open");
      const width = menu.offsetWidth;
      const height = menu.offsetHeight;
      menu.style.left =
        `${Math.min(clientX, window.innerWidth - width - 8)}px`;
      menu.style.top =
        `${Math.min(clientY, window.innerHeight - height - 8)}px`;
    }

    function hideGraphContextMenu() {
      $("graphContextMenu").classList.remove("open");
      graphContextModuleId = "";
    }

    function graphHandleContextAction(action) {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === graphContextModuleId);
      if (!module) return;
      const moduleId = module.id;
      hideGraphContextMenu();
      if (action === "configure") openGraphSettings(moduleId);
      else if (action === "inspect") openGraphInspector(moduleId);
      else if (action === "rename") {
        const name = prompt("Module name", module.name)?.trim();
        if (name && name !== module.name) {
          graphRecordUndo();
          module.name = name;
          graphMarkDirty();
          renderGraphEditor();
        }
      }
      else if (action === "duplicate") graphDuplicateModule(moduleId);
      else if (action === "toggle") {
        graphRecordUndo();
        module.enabled = module.enabled === false;
        graphMarkDirty();
        renderGraphEditor();
      }
      else if (action === "reset") {
        graphSetValidation(
          "Reset is currently graph-wide; resetting all module processing state.",
          null);
        controlGraphRuntime("reset", true);
      }
      else if (action === "remove") graphRemoveModule(moduleId);
    }

    function graphFinishConnection(event) {
      if (!graphConnectDraft) return;
      const targetElement = document.elementFromPoint(
        event.clientX,
        event.clientY)?.closest(".graph-port.input");
      if (targetElement) {
        const targetModule = graphDraft.modules.find(
          (module) => module.id === targetElement.dataset.moduleId);
        const targetDescriptor = graphType(targetModule?.typeId);
        const targetPort = targetDescriptor?.ports.find(
          (port) =>
            port.direction === "input" &&
            port.id === targetElement.dataset.portId);
        if (graphPortsCompatible(
          graphConnectDraft.source.moduleId,
          graphConnectDraft.sourcePort,
          targetModule?.id,
          targetPort)) {
          graphRecordUndo();
          graphDraft.connections = graphDraft.connections.filter(
            (connection) =>
              connection.target.moduleId !== targetModule.id ||
              connection.target.portId !== targetPort.id);
          graphDraft.connections.push({
            id: `connection-${crypto.randomUUID().slice(0, 12)}`,
            source: graphClone(graphConnectDraft.source),
            target: {
              moduleId: targetModule.id,
              portId: targetPort.id
            }
          });
          graphMarkDirty();
        }
      }
      graphConnectDraft = null;
      $("graphCanvas").classList.remove("connecting");
      document.querySelectorAll(
        ".graph-port.compatible, .graph-port.incompatible").forEach(
        (port) => port.classList.remove("compatible", "incompatible"));
      renderGraphEditor();
    }

    $("graphModuleSearch").addEventListener("input", renderGraphPalette);
    $("graphCanvas").addEventListener("dragover", (event) => {
      if (event.dataTransfer.types.includes(
        "application/x-pamguard-module")) {
        event.preventDefault();
        event.dataTransfer.dropEffect = "copy";
      }
    });
    $("graphCanvas").addEventListener("drop", (event) => {
      const typeId = event.dataTransfer.getData(
        "application/x-pamguard-module");
      if (!typeId) return;
      event.preventDefault();
      graphAddModuleAt(
        typeId,
        graphScreenToWorld(event.clientX, event.clientY));
    });
    $("graphCanvas").addEventListener("pointerdown", (event) => {
      if (event.target.closest(".graph-node, .graph-connection")) return;
      hideGraphContextMenu();
      graphSelectedNodeId = "";
      graphSelectedConnectionId = "";
      renderGraphSelection();
      if (event.button === 0 || event.button === 1) {
        graphPanDrag = {
          startX: event.clientX,
          startY: event.clientY,
          originX: graphLayout.viewport.x,
          originY: graphLayout.viewport.y
        };
        $("graphCanvas").classList.add("panning");
      }
    });
    $("graphCanvas").addEventListener("wheel", (event) => {
      event.preventDefault();
      graphSetZoom(
        graphLayout.viewport.zoom * (event.deltaY < 0 ? 1.1 : 0.9),
        event.clientX,
        event.clientY);
    }, { passive: false });
    window.addEventListener("pointermove", (event) => {
      if (graphNodeDrag) {
        const point = graphScreenToWorld(event.clientX, event.clientY);
        const position = graphLayout.positions[graphNodeDrag.moduleId];
        position.x = Math.max(
          0,
          Math.min(
            4000 - GRAPH_NODE_WIDTH,
            graphNodeDrag.origin.x + point.x - graphNodeDrag.start.x));
        position.y = Math.max(
          0,
          Math.min(
            2450,
            graphNodeDrag.origin.y + point.y - graphNodeDrag.start.y));
        const node = document.querySelector(
          `.graph-node[data-module-id="${CSS.escape(graphNodeDrag.moduleId)}"]`);
        if (node) {
          node.style.left = `${position.x}px`;
          node.style.top = `${position.y}px`;
        }
        renderGraphConnections();
      }
      else if (graphPanDrag) {
        graphLayout.viewport.x =
          graphPanDrag.originX + event.clientX - graphPanDrag.startX;
        graphLayout.viewport.y =
          graphPanDrag.originY + event.clientY - graphPanDrag.startY;
        applyGraphViewport();
      }
      else if (graphConnectDraft) {
        graphConnectDraft.pointer = graphScreenToWorld(
          event.clientX,
          event.clientY);
        renderGraphConnections();
      }
    });
    window.addEventListener("pointerup", (event) => {
      if (graphConnectDraft) graphFinishConnection(event);
      if (graphNodeDrag) {
        graphNodeDrag = null;
        saveGraphLayout();
      }
      if (graphPanDrag) {
        graphPanDrag = null;
        $("graphCanvas").classList.remove("panning");
        saveGraphLayout();
      }
    });
    window.addEventListener("resize", () =>
      requestAnimationFrame(renderGraphConnections));
    document.addEventListener("pointerdown", (event) => {
      if (!event.target.closest("#graphContextMenu")) {
        hideGraphContextMenu();
      }
    });
    $("graphCanvas").addEventListener("keydown", (event) => {
      if (event.key !== "Delete" && event.key !== "Backspace") return;
      if (graphSelectedNodeId) {
        event.preventDefault();
        graphRemoveModule(graphSelectedNodeId, false);
      }
      else if (graphSelectedConnectionId) {
        event.preventDefault();
        graphRecordUndo();
        graphDraft.connections = graphDraft.connections.filter(
          (connection) => connection.id !== graphSelectedConnectionId);
        graphSelectedConnectionId = "";
        graphMarkDirty();
        renderGraphEditor();
      }
    });
    $("graphUndo").addEventListener("click", graphUndo);
    $("graphRedo").addEventListener("click", graphRedo);
    $("graphZoomOut").addEventListener(
      "click",
      () => graphSetZoom(graphLayout.viewport.zoom / 1.15));
    $("graphZoomIn").addEventListener(
      "click",
      () => graphSetZoom(graphLayout.viewport.zoom * 1.15));
    $("graphFit").addEventListener("click", graphFitAll);
    $("graphAutoLayout").addEventListener("click", graphAutoLayout);
    $("graphContextMenu").addEventListener("click", (event) => {
      const action = event.target.closest("[data-graph-action]")
        ?.dataset.graphAction;
      if (action) graphHandleContextAction(action);
    });
    $("graphSettingsClose").addEventListener("click", closeGraphSettings);
    $("graphSettingsCancel").addEventListener("click", closeGraphSettings);
    $("graphSettingsSave").addEventListener("click", saveGraphSettings);
    $("graphSettingsDefaults").addEventListener("click", () => {
      const module = graphDraft?.modules.find(
        (candidate) => candidate.id === graphSettingsModuleId);
      const descriptor = module ? graphType(module.typeId) : null;
      if (!descriptor) return;
      graphSettingsDraft = graphClone(descriptor.defaultSettings || {});
      renderGraphSettingsDialog();
    });
    $("graphSettingsAdvanced").addEventListener("click", () => {
      try {
        graphSettingsDraft = graphReadSettingsForm();
        graphSettingsAdvancedMode = !graphSettingsAdvancedMode;
        renderGraphSettingsDialog();
      }
      catch (error) {
        graphSetValidation(`Settings error: ${error.message}`, false);
      }
    });
    $("graphInspectorClose").addEventListener(
      "click",
      () => $("graphInspectorDialog").close());
    $("graphInspectorDone").addEventListener(
      "click",
      () => $("graphInspectorDialog").close());
    $("graphRefresh").addEventListener("click", loadGraphEditor);
    $("graphValidate").addEventListener("click", validateGraphEditor);
    $("graphApply").addEventListener("click", applyGraphEditor);
    $("graphStart").addEventListener(
      "click",
      () => controlGraphRuntime("start"));
    $("graphStop").addEventListener(
      "click",
      () => controlGraphRuntime("stop"));
    $("graphFlush").addEventListener(
      "click",
      () => controlGraphRuntime("flush"));
    $("graphReset").addEventListener(
      "click",
      () => controlGraphRuntime("reset", true));
    $("graphOperatorSubmit").addEventListener("click", async () => {
      const moduleId = $("graphOperatorInput").value;
      const label = $("graphOperatorLabel").value.trim();
      if (!moduleId || !label) {
        $("graphOperatorMeta").textContent =
          "Select an applied operator module and enter a label.";
        return;
      }
      try {
        await requestJson(api(
          `/module-runtime/operator-inputs/${encodeURIComponent(moduleId)}/events`), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            label,
            notes: $("graphOperatorNotes").value,
            value: Number($("graphOperatorValue").value) || 0,
            timeMs: Date.now()
          })
        });
        $("graphOperatorLabel").value = "";
        $("graphOperatorNotes").value = "";
        $("graphOperatorMeta").textContent =
          `Recorded entry in ${moduleId} at ${new Date().toLocaleTimeString()}.`;
      }
      catch (error) {
        $("graphOperatorMeta").textContent = `Entry failed: ${error}`;
      }
    });

    let dataModelMounted = false;
    const dataModelController = Object.freeze({
      configure({
        onApplied,
        onAcquisitionListChanged
      } = {}) {
        if (dataModelMounted) {
          throw new Error(
            "Data Model callbacks cannot change while mounted");
        }
        if (onApplied !== undefined) {
          if (typeof onApplied !== "function") {
            throw new TypeError("onApplied must be a function");
          }
          dataModelCallbacks.onApplied = onApplied;
        }
        if (onAcquisitionListChanged !== undefined) {
          if (typeof onAcquisitionListChanged !== "function") {
            throw new TypeError(
              "onAcquisitionListChanged must be a function");
          }
          dataModelCallbacks.onAcquisitionListChanged =
            onAcquisitionListChanged;
        }
      },
      mount() {
        if (dataModelMounted) {
          throw new Error(
            "Data Model controller is already mounted");
        }
        dataModelMounted = true;
        loadGraphLayout();
        loadGraphEditor();
        captureStatusTimer = setInterval(
          () => refreshCaptureStatus({ quiet: true }),
          2000);
      },
      dispose() {
        if (!dataModelMounted) return;
        dataModelMounted = false;
        if (captureStatusTimer !== null) {
          clearInterval(captureStatusTimer);
          captureStatusTimer = null;
        }
        $("graphNodes").replaceChildren();
        $("graphWires").replaceChildren();
        $("graphPalette").replaceChildren();
      },
      load: loadGraphEditor,
      focusControlledUnit: graphFocusModule,
      get state() {
        return Object.freeze({
          graph: structuredClone(graphDraft),
          dirty: graphDirty,
          layout: structuredClone(graphLayout),
          runtime: structuredClone(graphRuntimeStatus)
        });
      }
    });

    globalThis.PamguardModules = Object.freeze({
      ...(globalThis.PamguardModules || {}),
      dataModel: dataModelController
    });
