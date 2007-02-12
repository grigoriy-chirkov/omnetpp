package org.omnetpp.ned.editor.graph;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.EventObject;
import java.util.List;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.IResourceChangeEvent;
import org.eclipse.core.resources.IResourceChangeListener;
import org.eclipse.core.resources.IResourceDelta;
import org.eclipse.core.resources.IResourceDeltaVisitor;
import org.eclipse.core.resources.IWorkspace;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IPath;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.SafeRunner;
import org.eclipse.draw2d.FigureCanvas;
import org.eclipse.draw2d.PositionConstants;
import org.eclipse.gef.ContextMenuProvider;
import org.eclipse.gef.DefaultEditDomain;
import org.eclipse.gef.EditPart;
import org.eclipse.gef.EditPartViewer;
import org.eclipse.gef.KeyHandler;
import org.eclipse.gef.KeyStroke;
import org.eclipse.gef.MouseWheelHandler;
import org.eclipse.gef.MouseWheelZoomHandler;
import org.eclipse.gef.dnd.TemplateTransferDragSourceListener;
import org.eclipse.gef.dnd.TemplateTransferDropTargetListener;
import org.eclipse.gef.editparts.ScalableRootEditPart;
import org.eclipse.gef.editparts.ZoomManager;
import org.eclipse.gef.palette.PaletteRoot;
import org.eclipse.gef.ui.actions.ActionRegistry;
import org.eclipse.gef.ui.actions.AlignmentAction;
import org.eclipse.gef.ui.actions.DirectEditAction;
import org.eclipse.gef.ui.actions.GEFActionConstants;
import org.eclipse.gef.ui.actions.MatchHeightAction;
import org.eclipse.gef.ui.actions.MatchWidthAction;
import org.eclipse.gef.ui.actions.ToggleSnapToGeometryAction;
import org.eclipse.gef.ui.actions.ZoomInAction;
import org.eclipse.gef.ui.actions.ZoomOutAction;
import org.eclipse.gef.ui.palette.PaletteViewer;
import org.eclipse.gef.ui.palette.PaletteViewerProvider;
import org.eclipse.gef.ui.parts.ContentOutlinePage;
import org.eclipse.gef.ui.parts.GraphicalEditorWithFlyoutPalette;
import org.eclipse.gef.ui.parts.GraphicalViewerKeyHandler;
import org.eclipse.gef.ui.parts.ScrollingGraphicalViewer;
import org.eclipse.gef.ui.parts.TreeViewer;
import org.eclipse.gef.ui.properties.UndoablePropertySheetEntry;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.dialogs.ProgressMonitorDialog;
import org.eclipse.jface.util.SafeRunnable;
import org.eclipse.jface.util.TransferDropTargetListener;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.swt.SWT;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IFileEditorInput;
import org.eclipse.ui.IPartListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchPartSite;
import org.eclipse.ui.actions.ActionFactory;
import org.eclipse.ui.actions.WorkspaceModifyOperation;
import org.eclipse.ui.dialogs.SaveAsDialog;
import org.eclipse.ui.part.FileEditorInput;
import org.eclipse.ui.part.IPageSite;
import org.eclipse.ui.part.MultiPageEditorSite;
import org.eclipse.ui.views.contentoutline.IContentOutlinePage;
import org.omnetpp.ned.editor.graph.actions.GNEDContextMenuProvider;
import org.omnetpp.ned.editor.graph.actions.ReLayoutAction;
import org.omnetpp.ned.editor.graph.actions.UnpinAction;
import org.omnetpp.ned.editor.graph.dnd.TextTransferDropTargetListener;
import org.omnetpp.ned.editor.graph.edit.NedEditPartFactory;
import org.omnetpp.ned.editor.graph.edit.outline.NedTreeEditPartFactory;
import org.omnetpp.ned.editor.graph.misc.ISelectionSupport;
import org.omnetpp.ned.editor.graph.misc.ModulePaletteCustomizer;
import org.omnetpp.ned.editor.graph.properties.view.BasePreferrerPropertySheetSorter;
import org.omnetpp.ned.editor.graph.properties.view.PropertySheetPageEx;
import org.omnetpp.ned.model.ex.NedFileNodeEx;
import org.omnetpp.ned.model.interfaces.IHasName;


public class GraphicalNedEditor extends GraphicalEditorWithFlyoutPalette 
    implements ISelectionSupport {
    
    public final static String MULTIPAGE_NEDEDITOR_ID = "org.omnetpp.ned.editor";
    
    class OutlinePage extends ContentOutlinePage {

        public OutlinePage(EditPartViewer viewer) {
            super(viewer);
        }

        @Override
        public void init(IPageSite pageSite) {
            super.init(pageSite);
            ActionRegistry registry = getActionRegistry();
            IActionBars bars = pageSite.getActionBars();
            String id = ActionFactory.UNDO.getId();
            bars.setGlobalActionHandler(id, registry.getAction(id));
            id = ActionFactory.REDO.getId();
            bars.setGlobalActionHandler(id, registry.getAction(id));
            id = ActionFactory.DELETE.getId();
            bars.setGlobalActionHandler(id, registry.getAction(id));
            bars.updateActionBars();
        }

        protected void configureOutlineViewer() {
            getViewer().setEditDomain(getEditDomain());
            getViewer().setEditPartFactory(new NedTreeEditPartFactory());
            ContextMenuProvider provider = new GNEDContextMenuProvider(getViewer(), getActionRegistry());
            getViewer().setContextMenu(provider);
            getSite().registerContextMenu("org.eclipse.gef.examples.logic.outline.contextmenu", //$NON-NLS-1$
                    provider, getSite().getSelectionProvider());
            getViewer().setKeyHandler(getCommonKeyHandler());
            getViewer().addDropTargetListener((TransferDropTargetListener)
        			new TemplateTransferDropTargetListener(getViewer()));
        }

        @Override
        public void createControl(Composite parent) {
            super.createControl(parent);
            configureOutlineViewer();
            getSelectionSynchronizer().addViewer(getViewer());
            setContents(getModel());
        }

        @Override
        public void dispose() {
            getSelectionSynchronizer().removeViewer(getViewer());
            super.dispose();
        }

        public void setContents(Object contents) {
            getViewer().setContents(contents);
        }

    }

    private KeyHandler sharedKeyHandler;
    private PaletteRoot root;
    private OutlinePage outlinePage;
    private boolean editorSaving = false;

    // This class listens to changes to the file system in the workspace, and
    // makes changes accordingly.
    // 1) An open, saved file gets deleted -> close the editor
    // 2) An open file gets renamed or moved -> change the editor's input
    // accordingly
    class ResourceTracker implements IResourceChangeListener, IResourceDeltaVisitor {
        public void resourceChanged(IResourceChangeEvent event) {
            IResourceDelta delta = event.getDelta();
            try {
                if (delta != null) delta.accept(this);
            } catch (CoreException exception) {
                // What should be done here?
            }
        }

        public boolean visit(IResourceDelta delta) {
            if (delta == null || !delta.getResource().equals(((IFileEditorInput) getEditorInput()).getFile()))
                return true;

            if (delta.getKind() == IResourceDelta.REMOVED) {
                Display display = getSite().getShell().getDisplay();
                if ((IResourceDelta.MOVED_TO & delta.getFlags()) == 0) {
                    // if the file was deleted
                    // NOTE: The case where an open, unsaved file is deleted is
                    // being handled by the PartListener added to the Workbench 
                    // in the initialize() method
                    display.asyncExec(new Runnable() {
                        public void run() {
                            if (!isDirty()) closeEditor(false);
                        }
                    });
                } else { // else if it was moved or renamed
                    final IFile newFile = ResourcesPlugin.getWorkspace().getRoot().getFile(
                            delta.getMovedToPath());
                    display.asyncExec(new Runnable() {
                        public void run() {
                            superSetInput(new FileEditorInput(newFile));
                        }
                    });
                }
            } else if (delta.getKind() == IResourceDelta.CHANGED) {
                if (!editorSaving) {
                    // the file was overwritten somehow (could have been
                    // replaced by another version in the respository)
                    final IFile newFile = ResourcesPlugin.getWorkspace().getRoot().getFile(
                            delta.getFullPath());
                    Display display = getSite().getShell().getDisplay();
                    display.asyncExec(new Runnable() {
                        public void run() {
                            setInput(new FileEditorInput(newFile));
                            getCommandStack().flush();
                        }
                    });
                }
            }
            return false;
        }
    }

    private IPartListener partListener = new IPartListener() {
        // If an open, unsaved file was deleted, query the user to either do a
        // "Save As"
        // or close the editor.
        public void partActivated(IWorkbenchPart part) {
            if (part != GraphicalNedEditor.this) return;
            if (!((IFileEditorInput) getEditorInput()).getFile().exists()) {
                Shell shell = getSite().getShell();
                String title = "File Deleted";
                String message = "The file has been deleted from the file system.";
                String[] buttons = { "Save", "Close" };
                MessageDialog dialog = new MessageDialog(shell, title, null, message, MessageDialog.QUESTION,
                        buttons, 0);
                if (dialog.open() == 0) {
                    if (!performSaveAs()) partActivated(part);
                } else {
                    closeEditor(false);
                }
            }
        }

        public void partBroughtToTop(IWorkbenchPart part) {
        }

        public void partClosed(IWorkbenchPart part) {
        }

        public void partDeactivated(IWorkbenchPart part) {
        }

        public void partOpened(IWorkbenchPart part) {
        }
    };

    private NedFileNodeEx nedFileModel;
    private ResourceTracker resourceListener = new ResourceTracker();

    protected static final String PALETTE_DOCK_LOCATION = "Dock location"; //$NON-NLS-1$
    protected static final String PALETTE_SIZE = "Palette Size"; //$NON-NLS-1$
    protected static final String PALETTE_STATE = "Palette state"; //$NON-NLS-1$
    protected static final int DEFAULT_PALETTE_SIZE = 130;

    public GraphicalNedEditor() {
        GraphicalNedEditorPlugin.getDefault().getPreferenceStore().setDefault(PALETTE_SIZE, DEFAULT_PALETTE_SIZE);
        setEditDomain(new DefaultEditDomain(this));
    }
    
    protected void closeEditor(boolean save) {
        getSite().getPage().closeEditor(GraphicalNedEditor.this, save);
    }

    @Override
    public void commandStackChanged(EventObject event) {
    	firePropertyChange(IEditorPart.PROP_DIRTY);
    	super.commandStackChanged(event);
    }

    @SuppressWarnings("unchecked")
	@Override
    protected void configureGraphicalViewer() {
        super.configureGraphicalViewer();
        ScrollingGraphicalViewer viewer = (ScrollingGraphicalViewer) getGraphicalViewer();

        ScalableRootEditPart root = new ScalableRootEditPart();

        List zoomLevels = new ArrayList(3);
        zoomLevels.add(ZoomManager.FIT_ALL);
        zoomLevels.add(ZoomManager.FIT_WIDTH);
        zoomLevels.add(ZoomManager.FIT_HEIGHT);
        root.getZoomManager().setZoomLevelContributions(zoomLevels);

        IAction zoomIn = new ZoomInAction(root.getZoomManager());
        IAction zoomOut = new ZoomOutAction(root.getZoomManager());
        getActionRegistry().registerAction(zoomIn);
        getActionRegistry().registerAction(zoomOut);
        
        // set the root edit part as the main viewer
        viewer.setRootEditPart(root);

        viewer.setEditPartFactory(new NedEditPartFactory());
        ContextMenuProvider provider = new GNEDContextMenuProvider(viewer, getActionRegistry());
        viewer.setContextMenu(provider);
        getSite().registerContextMenu("org.omnetpp.ned.editor.graph.contextmenu", provider, viewer);
        viewer.setKeyHandler(new GraphicalViewerKeyHandler(viewer).setParent(getCommonKeyHandler()));

        loadProperties();

        // Actions

        IAction snapAction = new ToggleSnapToGeometryAction(getGraphicalViewer());
        getActionRegistry().registerAction(snapAction);
        
//        Listener listener = new Listener() {
//            public void handleEvent(Event event) {
//                handleActivationChanged(event);
//            }
//        };
//        getGraphicalControl().addListener(SWT.Activate, listener);
//        getGraphicalControl().addListener(SWT.Deactivate, listener);
    }

//    @Override
//    protected CustomPalettePage createPalettePage() {
//        return new CustomPalettePage(getPaletteViewerProvider()) {
//            @Override
//            public void init(IPageSite pageSite) {
//                super.init(pageSite);
//                IAction copy = getActionRegistry().getAction(ActionFactory.COPY.getId());
//                pageSite.getActionBars().setGlobalActionHandler(ActionFactory.COPY.getId(), copy);
//            }
//        };
//    }

    @Override
    protected PaletteViewerProvider createPaletteViewerProvider() {
        return new PaletteViewerProvider(getEditDomain()) {

            @Override
            protected void configurePaletteViewer(PaletteViewer viewer) {
                super.configurePaletteViewer(viewer);
                viewer.setCustomizer(new ModulePaletteCustomizer());
                viewer.addDragSourceListener(new TemplateTransferDragSourceListener(viewer));
            }
        };
    }

    @Override
    public void dispose() {
        getSite().getWorkbenchWindow().getPartService().removePartListener(partListener);
        partListener = null;
        ((IFileEditorInput) getEditorInput()).getFile().getWorkspace().removeResourceChangeListener(
                resourceListener);
        super.dispose();
    }

    @Override
    public void doSave(final IProgressMonitor progressMonitor) {
    	editorSaving = true;
    	SafeRunner.run(new SafeRunnable() {
    		public void run() throws Exception {
    			saveProperties();
    			ByteArrayOutputStream out = new ByteArrayOutputStream();
// TODO save the contents of the editor
//    			writeToOutputStream(out);
    			IFile file = ((IFileEditorInput)getEditorInput()).getFile();
    			file.setContents(new ByteArrayInputStream(out.toByteArray()), 
    							true, false, progressMonitor);
    			getCommandStack().markSaveLocation();
    		}
    	});
    	editorSaving = false;
    }

    @Override
    public void doSaveAs() {
        performSaveAs();
    }

    @Override
    public Object getAdapter(Class type) {
        if (type == org.eclipse.ui.views.properties.IPropertySheetPage.class) {
            PropertySheetPageEx page = new PropertySheetPageEx();
            // customize the property sheet with a custom sorter (to place the "Base"
            // category at the beginning)
            page.setSorter(new BasePreferrerPropertySheetSorter());
            page.setRootEntry(new UndoablePropertySheetEntry(getCommandStack()));
            return page;
        }
        if (type == IContentOutlinePage.class) {
            if (outlinePage == null)
                outlinePage = new OutlinePage(new TreeViewer());
            return outlinePage;
        }
        if (type == ZoomManager.class) 
            return getGraphicalViewer().getProperty(ZoomManager.class.toString());

        return super.getAdapter(type);
    }

    /**
     * Returns the KeyHandler with common bindings for both the Outline and
     * Graphical Views. For example, delete is a common action.
     */
    protected KeyHandler getCommonKeyHandler() {
        if (sharedKeyHandler == null) {
            sharedKeyHandler = new KeyHandler();
            sharedKeyHandler.put(KeyStroke.getPressed(SWT.F2, 0), getActionRegistry().getAction(
                    GEFActionConstants.DIRECT_EDIT));
        }
        return sharedKeyHandler;
    }


    @Override
    protected PaletteRoot getPaletteRoot() {
        if (root == null) {
            root = GraphicalNedEditorPlugin.createPalette();
        }
        return root;
    }

//    protected void handleActivationChanged(Event event) {
//        IAction copy = null;
//        if (event.type == SWT.Deactivate) copy = getActionRegistry().getAction(ActionFactory.COPY.getId());
//        if (getEditorSite().getActionBars().getGlobalActionHandler(ActionFactory.COPY.getId()) != copy) {
//            getEditorSite().getActionBars().setGlobalActionHandler(ActionFactory.COPY.getId(), copy);
//            getEditorSite().getActionBars().updateActionBars();
//        }
//    }

    @Override
    protected void initializeGraphicalViewer() {
        super.initializeGraphicalViewer();
        getGraphicalViewer().setContents(getModel());
        
        // TODO do we need these?
        getGraphicalViewer().addDropTargetListener((TransferDropTargetListener)
    			new TemplateTransferDropTargetListener(getGraphicalViewer()));
        getGraphicalViewer().addDropTargetListener((TransferDropTargetListener)
        		new TextTransferDropTargetListener(getGraphicalViewer(), TextTransfer.getInstance()));
    }

    /* (non-Javadoc)
     * @see org.eclipse.gef.ui.parts.GraphicalEditor#createActions()
     * Register the used acions 
     */
    @SuppressWarnings("unchecked")
	@Override
    protected void createActions() {
        super.createActions();
        ActionRegistry registry = getActionRegistry();
        IAction action;

        action = new MatchWidthAction(this);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new MatchHeightAction(this);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

//        action = new ModulePasteTemplateAction(this);
//        registry.registerAction(action);
//        getSelectionActions().add(action.getId());

        action = new DirectEditAction((IWorkbenchPart) this);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.LEFT);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.RIGHT);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.TOP);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.BOTTOM);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.CENTER);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new AlignmentAction((IWorkbenchPart) this, PositionConstants.MIDDLE);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());
        
        action = new UnpinAction((IWorkbenchPart)this);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());

        action = new ReLayoutAction((IWorkbenchPart)this);
        registry.registerAction(action);
        getSelectionActions().add(action.getId());
    }

    protected FigureCanvas getEditor() {
        return (FigureCanvas) getGraphicalViewer().getControl();
    }

    @Override
    public boolean isSaveAsAllowed() {
        return true;
    }

    protected void loadProperties() {
        // Snap to Geometry property
//        getGraphicalViewer().setProperty(SnapToGeometry.PROPERTY_SNAP_ENABLED,
//                new Boolean(getModel().isSnapToGeometryEnabled()));
//
        // Zoom
//        ZoomManager manager = (ZoomManager) getGraphicalViewer().getProperty(ZoomManager.class.toString());
//        if (manager != null) manager.setZoom(getModel().getZoom());

        // Scroll-wheel Zoom support
        getGraphicalViewer().setProperty(MouseWheelHandler.KeyGenerator.getKey(SWT.MOD1),
                MouseWheelZoomHandler.SINGLETON);

    }

    protected boolean performSaveAs() {
        SaveAsDialog dialog = new SaveAsDialog(getSite().getWorkbenchWindow().getShell());
        dialog.setOriginalFile(((IFileEditorInput) getEditorInput()).getFile());
        dialog.open();
        IPath path = dialog.getResult();

        if (path == null) return false;

        IWorkspace workspace = ResourcesPlugin.getWorkspace();
        final IFile file = workspace.getRoot().getFile(path);

        if (!file.exists()) {
            WorkspaceModifyOperation op = new WorkspaceModifyOperation() {
                @Override
                public void execute(final IProgressMonitor monitor) {
                    saveProperties();
                    try {
                        ByteArrayOutputStream out = new ByteArrayOutputStream();
// TODO save the contents of the file                        
//                        writeToOutputStream(out);
                        file.create(new ByteArrayInputStream(out.toByteArray()), true, monitor);
                        out.close();
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            };
            try {
                new ProgressMonitorDialog(getSite().getWorkbenchWindow().getShell()).run(false, true, op);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        try {
            superSetInput(new FileEditorInput(file));
            getCommandStack().markSaveLocation();
        } catch (Exception e) {
            e.printStackTrace();
        }
        return true;
    }

    protected void saveProperties() {
//        getModel()
//                .setGridEnabled(
//                        ((Boolean) getGraphicalViewer().getProperty(SnapToGrid.PROPERTY_GRID_ENABLED))
//                                .booleanValue());
//        getModel().setSnapToGeometry(
//                ((Boolean) getGraphicalViewer().getProperty(SnapToGeometry.PROPERTY_SNAP_ENABLED))
//                        .booleanValue());
//        ZoomManager manager = (ZoomManager) getGraphicalViewer().getProperty(ZoomManager.class.toString());
//        if (manager != null) getModel().setZoom(manager.getZoom());
    }

    @Override
    protected void setInput(IEditorInput input) {
        superSetInput(input);
    }

    public NedFileNodeEx getModel() {
        return nedFileModel;
    }

    public void setModel(NedFileNodeEx nedModel) {
        nedFileModel = nedModel;

        if (!editorSaving) {
            if (getGraphicalViewer() != null) {
                getGraphicalViewer().setContents(getModel());
                loadProperties();
            }
            if (outlinePage != null) {
                outlinePage.setContents(getModel());
            }
        }
    }

    protected void superSetInput(IEditorInput input) {
        // The workspace never changes for an editor. So, removing and re-adding the
        // resourceListener is not necessary. But it is being done here for the sake
        // of proper implementation. Plus, the resourceListener needs to be added
        // to the workspace the first time around.
        if (getEditorInput() != null) {
            IFile file = ((IFileEditorInput) getEditorInput()).getFile();
            file.getWorkspace().removeResourceChangeListener(resourceListener);
        }

        super.setInput(input);

        if (getEditorInput() != null) {
            IFile file = ((IFileEditorInput) getEditorInput()).getFile();
            file.getWorkspace().addResourceChangeListener(resourceListener);
            setPartName(file.getName());
        }
    }

    @Override
    protected void setSite(IWorkbenchPartSite site) {
        super.setSite(site);
        getSite().getWorkbenchWindow().getPartService().addPartListener(partListener);
    }


    // XXX HACK OVERRIDDEN because selection does not work if the editor is embedded in a multipage editor
    // GraphicalEditor doesn't accept the selection change event if embedded in a multipage editor
    // hack can be removed once the issue corrected in GEF
    // see bug https://bugs.eclipse.org/bugs/show_bug.cgi?id=107703
    // see http://dev.eclipse.org/newslists/news.eclipse.tools.gef/msg10485.html
    @Override
    public void selectionChanged(IWorkbenchPart part, ISelection selection) {
        // update actions ONLY if we are the active editor or the parent editor which is a multipage editor
        IEditorPart activeEditor = getSite().getPage().getActiveEditor(); 
        if (this == activeEditor ||
              (getSite() instanceof MultiPageEditorSite && 
              ((MultiPageEditorSite)getSite()).getMultiPageEditor() == activeEditor))
            updateActions(getSelectionActions());
    }
    
    public void selectComponent(String componentName) {
        if (componentName == null || "".equals(componentName)) {
            getGraphicalViewer().deselectAll();
            return;
        }
        
        List toplevelParts = getGraphicalViewer().getContents().getChildren();
        EditPart selectedEditpart = null;
        for (Object child : toplevelParts) {
            Object model = ((EditPart)child).getModel();
            if ((model instanceof IHasName) && componentName.equals(((IHasName)model).getName()))
                    selectedEditpart = (EditPart)child;
        }
            
        getGraphicalViewer().reveal(selectedEditpart);
        getGraphicalViewer().select(selectedEditpart);
    }

}