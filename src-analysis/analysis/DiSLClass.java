package analysis;

import ch.usi.dag.disl.annotation.Before;
import ch.usi.dag.disl.dynamiccontext.DynamicContext;
import ch.usi.dag.disl.marker.BodyMarker;
import ch.usi.dag.dislre.REDispatch;


public class DiSLClass {

    @Before (marker = BodyMarker.class, scope = "test.TaggingTest.add")
    public static void add (final DynamicContext dc) {
        REDispatch.analysisStart (Local.ADD);
        REDispatch.sendObject (dc.getMethodArgumentValue (0, Object.class));
        REDispatch.analysisEnd ();
    }


    @Before (marker = BodyMarker.class, scope = "test.TaggingTest.remove")
    public static void remove (final DynamicContext dc) {
        REDispatch.analysisStart (Local.RMV);
        REDispatch.sendObject (dc.getMethodArgumentValue (0, Object.class));
        REDispatch.analysisEnd ();
    }

}
