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

	public static void constructorStart(Object forObject) {

        REDispatch.analysisStart(cs);

        REDispatch.sendObject(forObject);

        REDispatch.analysisEnd();
	}

	public static void constructorEnd() {
		
        REDispatch.analysisStart(ce);

        REDispatch.analysisEnd();
	}
    
    public static void onObjectAllocation(Object object, String allocationSite) {

        REDispatch.analysisStart(oa);

        REDispatch.sendObject(object);
        REDispatch.sendObjectPlusData(allocationSite);

        REDispatch.analysisEnd();

    }

    public static void onFieldRead(Object object, Class<?> ownerClass, String fieldId) {

        REDispatch.analysisStart(fr);

        REDispatch.sendObject(object);
        REDispatch.sendObjectPlusData(ownerClass);
        REDispatch.sendObjectPlusData(fieldId);

        REDispatch.analysisEnd();
    }

    public static void onFieldWrite(Object object, Class<?> ownerClass, String fieldId) {

        REDispatch.analysisStart(fw);

        REDispatch.sendObject(object);
        REDispatch.sendObjectPlusData(ownerClass);
        REDispatch.sendObjectPlusData(fieldId);

        REDispatch.analysisEnd();
    }
}
