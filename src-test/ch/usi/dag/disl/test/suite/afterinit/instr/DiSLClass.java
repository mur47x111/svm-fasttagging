package ch.usi.dag.disl.test.suite.afterinit.instr;

import ch.usi.dag.disl.annotation.After;
import ch.usi.dag.disl.annotation.Before;
import ch.usi.dag.disl.marker.AfterInitBodyMarker;

public class DiSLClass {
	
	@Before(marker = AfterInitBodyMarker.class, scope = "*TargetClass2.<init>")
	public static void after() {
		System.out.println("Before");
	}
	
	@After(marker = AfterInitBodyMarker.class, scope = "*TargetClass2.<init>")
	public static void afterThrowning() {
		System.out.println("After");
	}
}
