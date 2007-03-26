package org.omnetpp.ned.editor;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.IMarker;
import org.eclipse.core.resources.IResourceChangeEvent;
import org.eclipse.core.resources.IResourceChangeListener;
import org.eclipse.core.resources.IResourceDelta;
import org.eclipse.core.resources.IResourceDeltaVisitor;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.Assert;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.text.BadLocationException;
import org.eclipse.jface.text.IDocument;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.IFileEditorInput;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.ide.IGotoMarker;
import org.eclipse.ui.part.FileEditorInput;
import org.eclipse.ui.part.MultiPageEditorPart;
import org.eclipse.ui.views.contentoutline.ContentOutline;
import org.omnetpp.common.editor.ISelectionSupport;
import org.omnetpp.ned.editor.graph.GraphicalNedEditor;
import org.omnetpp.ned.editor.text.TextualNedEditor;
import org.omnetpp.ned.model.NEDTreeUtil;
import org.omnetpp.ned.model.ex.NedFileNodeEx;
import org.omnetpp.ned.resources.NEDResources;
import org.omnetpp.ned.resources.NEDResourcesPlugin;

/**
 * Multi-page NED editor.
 * 
 * TODO react to resource changes (reread if file changed, close if file deleted)
 * FIXME File|Open in Eclipse won't work!!! it creates a JavaFileEditorInput which is NOT an IFileEditorInput!!! 
 */
public class MultiPageNedEditor extends MultiPageEditorPart implements
		ISelectionSupport, IGotoMarker {

    private GraphicalNedEditor graphEditor;
	private TextualNedEditor nedEditor;
    private ResourceTracker resourceListener = new ResourceTracker();

    private String textContent = "";          // the text version of the file the last time we have switched editors
	private int graphPageIndex;
	private int textPageIndex;
	private boolean insidePageChange = false;
    private boolean initPhase = true;
	
	public void init(IEditorSite site, IEditorInput editorInput) throws PartInitException {
		super.init(site, editorInput);
		setPartName(editorInput.getName());
        if (!(editorInput instanceof IFileEditorInput))
            throw new PartInitException("Invalid input type!");
        
        NEDResourcesPlugin.getNEDResources().connect(((IFileEditorInput)editorInput).getFile());
	}
    
    @Override
    public void dispose() {
        ((IFileEditorInput)getEditorInput()).getFile()
                .getWorkspace().removeResourceChangeListener(resourceListener);
        NEDResourcesPlugin.getNEDResources().disconnect(((IFileEditorInput)getEditorInput()).getFile());
        super.dispose();
    }

	@Override
	protected void createPages() {
		graphEditor = new GraphicalNedEditor();
		nedEditor = new TextualNedEditor();
        IFile ifile = ((FileEditorInput)getEditorInput()).getFile();
        
		try {
            // setup graphical editor
			NEDResources res = NEDResourcesPlugin.getNEDResources();
            NedFileNodeEx modelRoot = (NedFileNodeEx)res.getNEDFileContents(ifile);
            graphEditor.setModel(modelRoot);
            graphPageIndex = addPage(graphEditor, getEditorInput());
            setPageText(graphPageIndex,"Graphical");

            // setup text editor
            // we don't have to set the content because it's set 
            // automatically by the text editor (from the FileEditorInput)
            textPageIndex = addPage(nedEditor, getEditorInput());
            setPageText(textPageIndex,"Text");

            // switch to graphics mode initially if there's no error in the file
            if (!res.containsNEDErrors(ifile))
                setActivePage(graphPageIndex);
            
		} catch (PartInitException e) {
			e.printStackTrace();  //XXX handle it? let it propagate?
		}
	}
	
	@Override
	protected void pageChange(int newPageIndex) {
	    //	prevent recursive call from setActivePage() below
	    if (insidePageChange)
	        return;
	    
	    insidePageChange = true;

        super.pageChange(newPageIndex);

        // XXX this is a MEGA hack because currently the workbench do not send a partActivated,deactivated messge
        // for the embedded editors in a MultiPageEditorView (this is a missing unimplemented feature, it works with MultiEditor however)
        // to make the nedded outline page active we should send activate/deactivate directly
        // we look for the outline view directy and send the notification by hand. once the MultiPageEditors are handled correctly
        // this can be removed
        // on each page change we emulate a close/open cycle of the multipage editor, this removed the associated
        // outline page, so the outline view will re-request the multipageeditor for a ContentOutlinePage (via getAdapter)
        // the current implementation of MultipageEditorPart.getAdapter delegates this request to the active 
        // embedded editor.
        ContentOutline coutline = (ContentOutline)getEditorSite().getPage().findView("org.eclipse.ui.views.ContentOutline");
        if (coutline != null) {
            // notify from the old closed editor
            coutline.partClosed(this);
            coutline.partActivated(this);
        }
        // end of the hack

        IFile ifile = ((FileEditorInput)getEditorInput()).getFile();
		NEDResources res = NEDResourcesPlugin.getNEDResources();
        // XXX FIXME this may be a way too slow as invalidates everything
        // it is needed only to display consistency errors correctly during page switching
        // it would be ok to invalidate only the components inside this file

        if (newPageIndex == textPageIndex) { 
			// switch from graphics to text:
			// generate text representation from the model
			NedFileNodeEx modelRoot = graphEditor.getModel();
            // generate the text representation
            textContent = NEDTreeUtil.generateNedSource(modelRoot, true);
            // put it into the text editor if changed
            if (!textContent.equals(nedEditor.getText())) {
                // TODO refresh the editor annotations to show the error marks
                res.setNEDFileText(ifile, textContent);
                nedEditor.setText(textContent);
            }
		} 
		else if (newPageIndex==graphPageIndex) {
			
		    // the text has changed in the editor
		    res.setNEDFileText(ifile, nedEditor.getText());
            if (!textContent.equals(nedEditor.getText())) {
				// set the parsed ned text as a model to the graphical editor 
				graphEditor.setModel((NedFileNodeEx)res.getNEDFileContents(ifile));
                // store the actual text content to be able th detect changes in the future
                textContent = nedEditor.getText();
			}

            // only start in graphics mode if there's no error in the file
            if (res.containsNEDErrors(ifile)) {
                // this happens if the parsing was unsuccessful when we wanted to switch from text to graph mode
				// parse error: switch back immediately to text view (we should never have 
				// switched away from it in the first place)
				setActivePage(textPageIndex);
                
                // set an empty content so next time we will try to parse the editor text again
                textContent = "";
				
				if (!initPhase) {
					MessageBox messageBox = new MessageBox(getEditorSite().getShell(), SWT.ICON_WARNING | SWT.OK);
					messageBox.setText("Warning");
					messageBox.setMessage("The editor contents has errors, switching is not possible. "+
					                      "Please fix the errors first.");
					messageBox.open();
	            }
			}
		}
		insidePageChange = false;
        initPhase = false;
	}

	@Override
	public void doSave(IProgressMonitor monitor) {
		if (getActivePage() == graphPageIndex) { 
			// switch from graphics to text:
			// generate text representation from the model
			NedFileNodeEx modelRoot = graphEditor.getModel();
            IFile ifile = ((FileEditorInput)getEditorInput()).getFile();
            
            // put the actual model state back to the incremental builder
    		NEDResourcesPlugin.getNEDResources().setNEDFileModel(ifile, modelRoot);
            
            // generate the text representation
            String textEditorContent = NEDTreeUtil.generateNedSource(modelRoot, true);
            // put it into the text editor
            nedEditor.setText(textEditorContent);
            graphEditor.getEditDomain().getCommandStack().markSaveLocation();
		}
		// delegate the save task to the TextEditor's save method
		nedEditor.doSave(monitor);
	}

    @Override
    public void doSaveAs() {
        // TODO add save as support
        Assert.isTrue(false, "save as not implemented");
    }
    
	@Override
	public boolean isSaveAsAllowed() {
        // we do not support save as...
		return false;
	}

    public void selectGraphComponent(String componentName) {
        graphEditor.selectGraphComponent(componentName);
    }

    /**
     * closes the editor and optionally saves it.
     * @param save
     */
    protected void closeEditor(boolean save) {
        getSite().getPage().closeEditor(this, save);
    }
    
    @Override
    protected void setInput(IEditorInput input) {
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

    // resource management open, close the editor depending on workspace notification
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
                            setInput(new FileEditorInput(newFile));
                        }
                    });
                }
            } else if (delta.getKind() == IResourceDelta.CHANGED) {
                // guard that we shoul dnot reload while save is in progress
//                if (!editorSaving) {
                  
                  // the file was overwritten somehow (could have been
                  // replaced by another version in the respository)
                  // TODO ask the user and reload the file
                
//                }
            }
            return false;
        }
    }

    public void gotoMarker(IMarker marker) {
        // switch to text page and delagte to it
        setActivePage(textPageIndex);
        IGotoMarker gm = (IGotoMarker)nedEditor.getAdapter(IGotoMarker.class);
        if (gm != null)
            gm.gotoMarker(marker);
    }
    
    public void setTextHighlightRange(int startLine, int endLine) {
        // switch to text page and delagte to it
        setActivePage(textPageIndex);
        IDocument document = nedEditor.getDocumentProvider().getDocument(getEditorInput());
        try {
            nedEditor.setHighlightRange(document.getLineOffset(startLine-1), 
                                        document.getLineOffset(endLine-1)+document.getLineLength(endLine-1), 
                                        true);
        } catch (BadLocationException e) {
        }
    }

}
