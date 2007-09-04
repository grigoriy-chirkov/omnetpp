package org.omnetpp.ned.editor.text.actions;

import org.eclipse.jface.text.BadLocationException;
import org.eclipse.jface.text.IDocument;
import org.eclipse.jface.text.ITextSelection;
import org.eclipse.jface.text.Position;
import org.eclipse.jface.text.source.IAnnotationModel;
import org.eclipse.jface.text.source.projection.ProjectionAnnotation;
import org.eclipse.jface.text.source.projection.ProjectionAnnotationModel;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.ui.texteditor.ITextEditor;

import org.omnetpp.common.editor.text.TextEditorAction;

/**
 * Allows to define a folding region in the ned file
 *
 * @author rhornig
 */
public class DefineFoldingRegionAction extends TextEditorAction {

    public static final String ID = "org.omnetpp.ned.editor.text.DefineFoldingRegion";

    public DefineFoldingRegionAction(ITextEditor editor) {
		super(editor);
        setId(ID);
        setActionDefinitionId(ID);
        setText("Define Folding Region");
        setDescription("Define a folding region in the source");
        setToolTipText(getDescription());
    }

	private IAnnotationModel getAnnotationModel(ITextEditor editor) {
		return (IAnnotationModel) editor.getAdapter(ProjectionAnnotationModel.class);
	}

    @Override
    public void update() {
        ITextEditor editor= getTextEditor();
        ISelection selection= editor.getSelectionProvider().getSelection();
        if (selection instanceof ITextSelection) {
            ITextSelection textSelection= (ITextSelection) selection;
            // allow folding action only if the selection start and end line is different
            if (textSelection.getStartLine() == textSelection.getEndLine()) {
                setEnabled(false);
                return;
            }
        }

        setEnabled(true);
    }

	/*
	 * @see org.eclipse.jface.action.Action#run()
	 */
	@Override
	public void run() {
		ITextEditor editor= getTextEditor();
		ISelection selection= editor.getSelectionProvider().getSelection();
		if (selection instanceof ITextSelection) {
			ITextSelection textSelection= (ITextSelection) selection;
			if (!textSelection.isEmpty()) {
				IAnnotationModel model= getAnnotationModel(editor);
				if (model != null) {

					int start= textSelection.getStartLine();
					int end= textSelection.getEndLine();

					try {
						IDocument document= editor.getDocumentProvider().getDocument(editor.getEditorInput());
						int offset= document.getLineOffset(start);
						int endOffset= document.getLineOffset(end + 1);
						Position position= new Position(offset, endOffset - offset);
						model.addAnnotation(new ProjectionAnnotation(), position);
					} catch (BadLocationException x) {
						// ignore
					}
				}
			}
		}
	}
}