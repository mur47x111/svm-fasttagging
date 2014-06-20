package instrument;

import observe.ImmutabilityAnalysisRE;
import ch.usi.dag.disl.annotation.After;
import ch.usi.dag.disl.annotation.AfterReturning;
import ch.usi.dag.disl.annotation.Before;
import ch.usi.dag.disl.annotation.GuardMethod;
import ch.usi.dag.disl.classcontext.ClassContext;
import ch.usi.dag.disl.dynamiccontext.DynamicContext;
import ch.usi.dag.disl.marker.BodyMarker;
import ch.usi.dag.disl.marker.BytecodeMarker;
import ch.usi.dag.disl.staticcontext.MethodStaticContext;

public class DiSLClass {

    /** CONSTRUCTORS *************************************************************/

    @Before(marker = BodyMarker.class, guard = ConstructorGuard.class)
    public static void beforeConstructor(DynamicContext dc) {
        ImmutabilityAnalysisRE.constructorStart(dc.getThis());
    }

    @After(marker = BodyMarker.class, guard = ConstructorGuard.class)
    public static void afterConstructor() {
    	ImmutabilityAnalysisRE.constructorEnd();
    }

    public static class ConstructorGuard {

        @GuardMethod
        public static boolean isApplicable(MethodStaticContext sc) {
            return "<init>".equals(sc.thisMethodName());
        }
    }

    /** FIELD ACCESSES ***********************************************************/

    @Before(marker=BytecodeMarker.class, args = "getfield")
    public static void beforeFieldRead(FieldAccessStaticContext sc, DynamicContext dc, ClassContext cc) {
        
    	final Object object = dc.getStackValue(0, Object.class);
    	
    	if(object != null) {
    		ImmutabilityAnalysisRE.onFieldRead(object, cc.asClass(sc.getOwner()), sc.getFieldId());
    	}
    }

    @Before(marker = BytecodeMarker.class, args = "putfield")
    public static void beforeFieldWrite(FieldAccessStaticContext sc, DynamicContext dc, ClassContext cc) {
        
    	final Object object = dc.getStackValue(1, Object.class);
        
        if(object != null) {
        	ImmutabilityAnalysisRE.onFieldWrite(object, cc.asClass(sc.getOwner()), sc.getFieldId());
        }
    }

    /** SHADOW HEAP **************************************************************/

    /**
     * Instruments the allocation of a single object (<code>new</code>)
     * 
     * Note that this snippet requires that bytecode verification is switched off (<code>-noverify</code>) as the newly
     * created instances are passed to the runtime <em>prior</em> to initialization.
     */
    @AfterReturning(marker = BytecodeMarker.class, args = "new")
    public static void objectAllocated(DynamicContext dc, AllocationSiteStaticContext sc, MethodStaticContext msc) {
        ImmutabilityAnalysisRE.onObjectAllocation(dc.getStackValue(0, Object.class), sc.getAllocationSite());
    }

    /**
     * Instruments the reflective allocation of a single object through a call to
     * {@link java.lang.reflect.Constructor#newInstance(Object...) Constructor.newInstance(Object...)}.
     * 
     * Note that this method assumes that {@link Class#newInstance()} internally delegates to
     * {@code Constructor.newInstance(Object...)}; hence, it only instruments the latter. This assumption is true under
     * OpenJDK and possibly other JREs.
     */
    @AfterReturning(marker = BodyMarker.class,
            scope = "java.lang.Object java.lang.reflect.Constructor.newInstance(java.lang.Object[])")
    public static void objectAllocatedThroughReflection(DynamicContext dc, AllocationSiteStaticContext sc) {
        ImmutabilityAnalysisRE.onObjectAllocation(dc.getStackValue(0, Object.class), sc.getReflectiveAllocationSite());
    }

}
