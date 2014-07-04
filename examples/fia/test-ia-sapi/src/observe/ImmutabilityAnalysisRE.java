package observe;

import ch.usi.dag.dislre.REDispatch;

public class ImmutabilityAnalysisRE {

    public static short cs = REDispatch.registerMethod(
            "analyse.ImmutabilityAnalysis.constructorStart");

    public static short ce = REDispatch.registerMethod(
            "analyse.ImmutabilityAnalysis.constructorEnd");

    public static short oa = REDispatch.registerMethod(
            "analyse.ImmutabilityAnalysis.onObjectAllocation");

    public static short fr = REDispatch.registerMethod(
            "analyse.ImmutabilityAnalysis.onFieldRead");

    public static short fw = REDispatch.registerMethod(
            "analyse.ImmutabilityAnalysis.onFieldWrite");


    public static void constructorStart (final Object forObject) {
        // REDispatch.analysisStart(cs);
        // REDispatch.sendObject(forObject);
        // REDispatch.analysisEnd();
        REDispatch.analyzeO (cs, forObject);
    }


    public static void constructorEnd () {
        // REDispatch.analysisStart(ce);
        // REDispatch.analysisEnd();
        REDispatch.analyze (ce);
    }


    public static void onObjectAllocation (
        final Object object, final String allocationSite) {
        // REDispatch.analysisStart (oa);
        // REDispatch.sendObject (object);
        // REDispatch.sendObjectPlusData (allocationSite);
        // REDispatch.analysisEnd ();

        REDispatch.analyzeOD (oa, object, allocationSite);
    }


    public static void onFieldRead (
        final Object object, final Class <?> ownerClass, final String fieldId) {
        // REDispatch.analysisStart (fr);
        // REDispatch.sendObject (object);
        // REDispatch.sendObjectPlusData (ownerClass);
        // REDispatch.sendObjectPlusData (fieldId);
        // REDispatch.analysisEnd ();

        REDispatch.analyzeODD (fr, object, ownerClass, fieldId);
    }


    public static void onFieldWrite (
        final Object object, final Class <?> ownerClass, final String fieldId) {
        // REDispatch.analysisStart (fw);
        // REDispatch.sendObject (object);
        // REDispatch.sendObjectPlusData (ownerClass);
        // REDispatch.sendObjectPlusData (fieldId);
        // REDispatch.analysisEnd ();

        REDispatch.analyzeODD (fw, object, ownerClass, fieldId);
    }
}
