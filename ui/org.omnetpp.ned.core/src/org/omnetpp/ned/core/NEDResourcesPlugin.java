package org.omnetpp.ned.core;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.Assert;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.omnetpp.common.editor.EditorUtil;
import org.omnetpp.ned.model.INEDElement;
import org.omnetpp.ned.model.interfaces.INEDTypeInfo;
import org.osgi.framework.BundleContext;

/**
 * The main plugin class to be used in the desktop.
 *
 * @author rhornig
 */
public class NEDResourcesPlugin extends AbstractUIPlugin {

    public static String PLUGIN_ID;

	// The shared instance.
	private static NEDResourcesPlugin plugin;

	// The actual NED type resolver
	private NEDResources nedResources;

    // The actual MSG type resolver
    private MsgResources msgResources;

	/**
	 * The constructor.
	 */
	public NEDResourcesPlugin() {
		plugin = this;
		nedResources = new NEDResources();
        msgResources = new MsgResources();
	}

	/**
	 * This method is called upon plug-in activation
	 */
	@Override
    public void start(BundleContext context) throws Exception {
		super.start(context);
		
        PLUGIN_ID = getBundle().getSymbolicName();
        // System.out.println("NEDResourcesPlugin started");

        nedResources.rebuildProjectsTable();
        msgResources.readAllMsgFiles();
        ResourcesPlugin.getWorkspace().addResourceChangeListener(NEDResourcesPlugin.getNEDResources());
	}

	/**
	 * This method is called when the plug-in is stopped
	 */
	@Override
    public void stop(BundleContext context) throws Exception {
        ResourcesPlugin.getWorkspace().removeResourceChangeListener(NEDResourcesPlugin.getNEDResources());
		plugin = null;
        super.stop(context);
	}

	/**
	 * Returns the shared instance.
	 */
	public static NEDResourcesPlugin getDefault() {
		return plugin;
	}

	/**
	 * Returns the NED file cache of the shared instance of the plugin
	 */
	public static NEDResources getNEDResources() {
		return plugin.nedResources;
	}

    /**
     * Returns the MSG file cache of the shared instance of the plugin
     */
    public static MsgResources getMSGResources() {
        return plugin.msgResources;
    }

	/**
	 * Returns an image descriptor for the image file at the given
	 * plug-in relative path.
	 */
	public static ImageDescriptor getImageDescriptor(String path) {
		return AbstractUIPlugin.imageDescriptorFromPlugin(PLUGIN_ID, path);
	}

	public static void logError(Throwable exception) {
		logError(exception.toString(), exception);
	}

	public static void logError(String message, Throwable exception) {
		if (plugin != null) {
			plugin.getLog().log(new Status(IStatus.ERROR, PLUGIN_ID, 0, message, exception));
		}
		else {
			System.err.println(message);
			exception.printStackTrace();
		}
	}

	/**
	 * Opens the given INEDElement in a NED editor, and positions the cursor on it.
	 * @param element must NOT be null, and MUST be part of the model (i.e. in NEDResourcesPlugin)
	 */
	public static void openNEDElementInEditor(INEDElement element) {
	    Assert.isNotNull(element);
        openNEDElementInEditor(element, IGotoNedElement.Mode.AUTOMATIC);
	}

    /**
     * Opens the given INEDElement in a NED editor, and positions the cursor on it.
     *
     * @param element must NOT be null, and MUST be part of the model (i.e. in NEDResourcesPlugin)
     * @param mode IGotoNedElement.Mode  whether the editor should be opened in text or graphical mode
     *             or in automatic mode
     */
    public static void openNEDElementInEditor(INEDElement element, IGotoNedElement.Mode mode) {
        INEDTypeInfo typeInfo = element.getSelfOrEnclosingTypeElement().getNEDTypeInfo();
        IFile file = typeInfo.getNEDFile();

        // check if file is null. it is a built in type in this case
        if (file == null) {
            MessageDialog.openInformation(Display.getDefault().getActiveShell(),
                    "Cannot Open Type", "Built-in types cannot be opened for editing.");
            return;
        }


        try {
            IEditorPart editor = EditorUtil.openEditor(file, true);

            // select the component so it will be visible in the opened editor
            if (editor instanceof IGotoNedElement) {
                ((IGotoNedElement)editor).showInEditor(element, mode);
            }
        } catch (PartInitException e) {
            // no message dialog is needed, because the platform displays an erroreditpart anyway
            logError("Cannot open NED editor", e);
        }
    }

}
